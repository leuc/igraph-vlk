/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "graph/stream.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_state.h"
#include "graph/community_simhash.h"
#include "graph/dyn_core_tree.h"
#include "graph/dyn_k-core.h"
#include "graph/dyn_layered_sphere.h"
#include "graph/dyn_leiden.h"
#include "graph/ncol_parse.h"
#include "graph/wrappers_community.h"
#include "os/stream.h"
#include "ui/menu.h"

#define GRAPH_STREAM_LINE_BATCH 256
#define GRAPH_STREAM_MAX_LINES_PER_FRAME 5000
#define GRAPH_STREAM_GRID_SIDE 32
#define GRAPH_STREAM_GRID_SPACING 2.0
#define GRAPH_STREAM_DEBUG_REPORT_INTERVAL_SEC 2.0

// ============================================================================
// Name -> vertex-id map (open addressing, linear probing, FNV-1a).
// No deletions: streaming only ever adds vertices.
// ============================================================================

typedef struct
{
	char *key; // NULL => empty slot
	igraph_integer_t vid;
} NameMapEntry;

typedef struct
{
	NameMapEntry *entries;
	size_t capacity; // always a power of two
	size_t count;
} NameMap;

static uint64_t fnv1a_hash(const char *s)
{
	uint64_t h = 1469598103934665603ULL;
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 1099511628211ULL;
	}
	return h;
}

static bool name_map_init(NameMap *m, size_t initial_capacity)
{
	m->capacity = initial_capacity;
	m->count = 0;
	m->entries = calloc(m->capacity, sizeof(NameMapEntry));
	return m->entries != NULL;
}

static bool name_map_lookup(const NameMap *m, const char *name, igraph_integer_t *out_vid)
{
	if (m->capacity == 0)
		return false;
	size_t mask = m->capacity - 1;
	size_t idx = (size_t)fnv1a_hash(name) & mask;
	for (size_t probe = 0; probe < m->capacity; probe++) {
		const NameMapEntry *e = &m->entries[idx];
		if (!e->key)
			return false;
		if (strcmp(e->key, name) == 0) {
			*out_vid = e->vid;
			return true;
		}
		idx = (idx + 1) & mask;
	}
	return false;
}

// Inserts an already-owned key (no strdup); used both for fresh inserts and
// for re-inserting entries during a rehash.
static void name_map_insert_raw(NameMap *m, char *owned_key, igraph_integer_t vid)
{
	size_t mask = m->capacity - 1;
	size_t idx = (size_t)fnv1a_hash(owned_key) & mask;
	while (m->entries[idx].key)
		idx = (idx + 1) & mask;
	m->entries[idx].key = owned_key;
	m->entries[idx].vid = vid;
	m->count++;
}

static bool name_map_grow(NameMap *m)
{
	size_t new_capacity = m->capacity * 2;
	NameMapEntry *new_entries = calloc(new_capacity, sizeof(NameMapEntry));
	if (!new_entries)
		return false;

	NameMapEntry *old_entries = m->entries;
	size_t old_capacity = m->capacity;
	m->entries = new_entries;
	m->capacity = new_capacity;
	m->count = 0;

	for (size_t i = 0; i < old_capacity; i++)
		if (old_entries[i].key)
			name_map_insert_raw(m, old_entries[i].key, old_entries[i].vid);

	free(old_entries);
	return true;
}

static bool name_map_insert(NameMap *m, const char *name, igraph_integer_t vid)
{
	if ((m->count + 1) * 10 >= m->capacity * 7) { // load factor 0.7
		if (!name_map_grow(m))
			return false;
	}
	char *owned = strdup(name);
	if (!owned)
		return false;
	name_map_insert_raw(m, owned, vid);
	return true;
}

static void name_map_destroy(NameMap *m)
{
	if (!m->entries)
		return;
	for (size_t i = 0; i < m->capacity; i++)
		free(m->entries[i].key);
	free(m->entries);
	m->entries = NULL;
	m->capacity = 0;
	m->count = 0;
}

// ============================================================================
// GraphStream state
// ============================================================================

struct GraphStream
{
	OsStreamReader *reader;
	NameMap names;
	DynKCore *kcore;				  // live coreness maintenance; NULL if init failed (non-fatal)
	int last_max_core;				  // max coreness at the last node-size mapping
	DynCoreTree *core_tree;			  // live k-core hierarchy maintenance (consumed by layered_sphere for sphere assignment); NULL if init failed (non-fatal). Independent of kcore above — see graph/dyn_core_tree.h's own DynKCore instance; the redundant coreness computation is an accepted tradeoff (dyn_core_tree.h's file header explains why it can't share kcore's).
	DynLeiden *leiden;				  // live community maintenance; NULL if init failed (non-fatal)
	DynLayeredSphere *layered_sphere; // live Layered Sphere layout maintenance; NULL if init failed (non-fatal)
	bool layered_sphere_enabled;	  // user-toggled, on by default (see "Data/Stream > [x] Live Layered Sphere")
	uint32_t node_capacity;			  // amortized capacity of data->nodes[]
	uint32_t edge_capacity;			  // amortized capacity of data->edges[]
	bool fatal_error;				  // set on unrecoverable error; poll() becomes a no-op

	// Debug totals: running counts since streaming started, reported to
	// stderr on a throttle (see stream_debug_report()).
	uint64_t total_vertices_streamed;
	uint64_t total_edges_streamed;
	uint64_t total_kcore_updates;	  // sum of vertices whose coreness changed
	uint64_t total_leiden_updates;	  // sum of vertices whose community changed
	uint64_t vertices_at_last_report; // snapshots for interval-delta reporting
	uint64_t edges_at_last_report;
	uint64_t kcore_updates_at_last_report;
	uint64_t leiden_updates_at_last_report;
	double last_debug_report_time;

	// Per-poll scratch space for edge weights, indexed in lockstep with the
	// pairs pushed into the poll's flat igraph_vector_int_t edge batch.
	double pend_weight[GRAPH_STREAM_MAX_LINES_PER_FRAME];
	bool pend_has_weight[GRAPH_STREAM_MAX_LINES_PER_FRAME];
};

// ============================================================================
// GraphData array capacity growth (doubling, matches std container amortization)
// ============================================================================

static bool ensure_node_capacity(GraphStream *gs, GraphData *data, uint32_t needed)
{
	if (needed <= gs->node_capacity)
		return true;
	uint32_t cap = gs->node_capacity ? gs->node_capacity : 64;
	while (cap < needed)
		cap *= 2;
	Node *tmp = realloc(data->nodes, sizeof(Node) * cap);
	if (!tmp) {
		fprintf(stderr, "graph_stream: realloc nodes[] failed (cap %u)\n", cap);
		return false;
	}
	data->nodes = tmp;
	gs->node_capacity = cap;
	return true;
}

static bool ensure_edge_capacity(GraphStream *gs, GraphData *data, uint32_t needed)
{
	if (needed <= gs->edge_capacity)
		return true;
	uint32_t cap = gs->edge_capacity ? gs->edge_capacity : 64;
	while (cap < needed)
		cap *= 2;
	Edge *tmp = realloc(data->edges, sizeof(Edge) * cap);
	if (!tmp) {
		fprintf(stderr, "graph_stream: realloc edges[] failed (cap %u)\n", cap);
		return false;
	}
	data->edges = tmp;
	gs->edge_capacity = cap;
	return true;
}

// ============================================================================
// NCOL line parsing: "<name1> <name2> [<weight>]"
// Malformed lines are logged and skipped, per this codebase's error convention.
// ============================================================================

// ============================================================================
// Vertex registration: cheap, called per new name, immediate (not batched —
// igraph_add_vertices() is O(1)-ish per call, unlike igraph_add_edges()).
// ============================================================================

static bool ensure_vertex(GraphStream *gs, GraphData *data, const char *name, igraph_integer_t *out_vid)
{
	igraph_integer_t vid;
	if (name_map_lookup(&gs->names, name, &vid)) {
		*out_vid = vid;
		return true;
	}

	vid = (igraph_integer_t)data->node_count;
	if (igraph_add_vertices(&data->g, 1, NULL) != IGRAPH_SUCCESS) {
		fprintf(stderr, "graph_stream: igraph_add_vertices failed for \"%s\"\n", name);
		return false;
	}
	if (SETVAS(&data->g, "name", vid, name) != IGRAPH_SUCCESS)
		fprintf(stderr, "graph_stream: SETVAS name failed for vertex %lld (\"%s\")\n", (long long)vid, name);

	// Real arrival time, not just an ordinal: the natural slot a genuine
	// timestamp from the data source (e.g. an NCOL time field) would occupy
	// later — see graph/dyn_layered_sphere.c, which reads this to stabilize
	// a vertex's placement relative to its community-mates.
	struct timespec arrival_ts;
	clock_gettime(CLOCK_MONOTONIC, &arrival_ts);
	double timestamp = (double)arrival_ts.tv_sec + (double)arrival_ts.tv_nsec * 1e-9;
	if (SETVAN(&data->g, "timestamp", vid, timestamp) != IGRAPH_SUCCESS)
		fprintf(stderr, "graph_stream: SETVAN timestamp failed for vertex %lld (\"%s\")\n", (long long)vid, name);

	if (!ensure_node_capacity(gs, data, data->node_count + 1))
		return false;

	Node *n = &data->nodes[vid];
	n->position[0] = 0.0f;
	n->position[1] = 0.0f;
	n->position[2] = 0.0f;
	n->color[0] = 0.6f;
	n->color[1] = 0.6f;
	n->color[2] = 0.6f;
	n->size = 1.0f;
	n->label = strdup(name);
	n->degree = 0;
	n->selected = 0.0f;
	n->visible = 1.0f;

	data->node_count++;
	data->props.node_count = (int)data->node_count;

	if (!name_map_insert(&gs->names, name, vid)) {
		// Vertex already exists in igraph_t + GraphData with no clean rollback
		// path (igraph has no "undo last add_vertices"); halt streaming rather
		// than risk an inconsistent name->id mapping.
		fprintf(stderr, "graph_stream: name map insert failed for \"%s\" (out of memory) — streaming halted\n", name);
		gs->fatal_error = true;
		return false;
	}

	*out_vid = vid;
	return true;
}

// ============================================================================
// Public API
// ============================================================================

GraphStream *graph_stream_init(GraphData *data)
{
	igraph_set_attribute_table(&igraph_cattribute_table);

	if (igraph_empty(&data->g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS) {
		fprintf(stderr, "graph_stream_init: igraph_empty failed\n");
		return NULL;
	}
	data->graph_initialized = true;
	data->node_attr_name = strdup("pagerank");
	data->nodes = NULL;
	data->node_count = 0;
	data->edges = NULL;
	data->edge_count = 0;
	data->hubs = NULL;
	data->hub_count = 0;
	data->filterable_attrs = NULL;
	data->num_filterable_attrs = 0;
	data->props.node_count = 0;
	data->props.edge_count = 0;
	data->props.coreness_filter = 0;
	data->use_as_seed = false;

	if (igraph_matrix_init(&data->current_layout, 0, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "graph_stream_init: igraph_matrix_init current_layout failed\n");
		igraph_destroy(&data->g);
		data->graph_initialized = false;
		free(data->node_attr_name);
		data->node_attr_name = NULL;
		return NULL;
	}

	GraphStream *gs = malloc(sizeof(GraphStream));
	if (!gs) {
		fprintf(stderr, "graph_stream_init: malloc failed\n");
		igraph_matrix_destroy(&data->current_layout);
		igraph_destroy(&data->g);
		data->graph_initialized = false;
		free(data->node_attr_name);
		data->node_attr_name = NULL;
		return NULL;
	}
	gs->node_capacity = 0;
	gs->edge_capacity = 0;
	gs->fatal_error = false;
	gs->total_vertices_streamed = 0;
	gs->total_edges_streamed = 0;
	gs->total_kcore_updates = 0;
	gs->total_leiden_updates = 0;
	gs->vertices_at_last_report = 0;
	gs->edges_at_last_report = 0;
	gs->kcore_updates_at_last_report = 0;
	gs->leiden_updates_at_last_report = 0;
	gs->last_debug_report_time = 0.0;

	if (!name_map_init(&gs->names, 64)) {
		fprintf(stderr, "graph_stream_init: name_map_init failed\n");
		free(gs);
		igraph_matrix_destroy(&data->current_layout);
		igraph_destroy(&data->g);
		data->graph_initialized = false;
		free(data->node_attr_name);
		data->node_attr_name = NULL;
		return NULL;
	}

	gs->kcore = dyn_kcore_init(&data->g);
	if (!gs->kcore)
		fprintf(stderr, "graph_stream_init: dynamic k-core maintenance unavailable\n");
	gs->last_max_core = 0;

	gs->leiden = dyn_leiden_init(&data->g);
	if (!gs->leiden)
		fprintf(stderr, "graph_stream_init: dynamic Leiden community maintenance unavailable\n");

	gs->core_tree = dyn_core_tree_init(&data->g);
	if (!gs->core_tree)
		fprintf(stderr, "graph_stream_init: dynamic k-core hierarchy maintenance unavailable\n");

	gs->layered_sphere = dyn_layered_sphere_init(&data->g, gs->core_tree, gs->leiden ? dyn_leiden_membership(gs->leiden) : NULL, &data->current_layout);
	if (!gs->layered_sphere)
		fprintf(stderr, "graph_stream_init: dynamic Layered Sphere layout maintenance unavailable\n");
	gs->layered_sphere_enabled = true;

	gs->reader = os_stream_reader_start();
	if (!gs->reader) {
		fprintf(stderr, "graph_stream_init: failed to start stdin reader thread\n");
		dyn_kcore_destroy(gs->kcore);
		dyn_core_tree_destroy(gs->core_tree);
		dyn_leiden_destroy(gs->leiden);
		dyn_layered_sphere_destroy(gs->layered_sphere);
		name_map_destroy(&gs->names);
		free(gs);
		igraph_matrix_destroy(&data->current_layout);
		igraph_destroy(&data->g);
		data->graph_initialized = false;
		free(data->node_attr_name);
		data->node_attr_name = NULL;
		return NULL;
	}

	return gs;
}

// ============================================================================
// Mirror maintained coreness into node sizes (NODE_SIZE_MIN..NODE_SIZE_MAX,
// the same mapping as apply_centrality_scores). Touches only vertices whose
// coreness changed this poll; rescales everything only when the max coreness
// rises, which happens at most max-coreness times over a whole stream.
// ============================================================================

static void stream_apply_coreness_sizes(GraphStream *gs, GraphData *data, const igraph_vector_int_t *changed)
{
	const int *core = dyn_kcore_values(gs->kcore);
	int maxk = dyn_kcore_max(gs->kcore);
	if (!core || maxk < 1 || !data->nodes)
		return;

	float scale = (NODE_SIZE_MAX - NODE_SIZE_MIN) / (float)maxk;
	if (maxk != gs->last_max_core) {
		gs->last_max_core = maxk;
		for (uint32_t i = 0; i < data->node_count; i++)
			data->nodes[i].size = NODE_SIZE_MIN + (float)core[i] * scale;
	} else {
		igraph_integer_t n = igraph_vector_int_size(changed);
		for (igraph_integer_t i = 0; i < n; i++) {
			igraph_integer_t vid = VECTOR(*changed)[i];
			if (vid >= 0 && vid < (igraph_integer_t)data->node_count)
				data->nodes[vid].size = NODE_SIZE_MIN + (float)core[vid] * scale;
		}
	}
}

// ============================================================================
// Mirror maintained community membership into node colors. Unlike coreness
// sizes, a community's color depends only on its own id (golden-ratio hue
// stepping, see community_id_to_rgb()), never on a global maximum, so there
// is no "rescale everything" case to handle here.
// ============================================================================

static void stream_apply_community_colors(GraphStream *gs, GraphData *data, const igraph_vector_int_t *changed)
{
	const igraph_integer_t *comm = dyn_leiden_membership(gs->leiden);
	if (!comm || !data->nodes)
		return;

	igraph_integer_t vcount = igraph_vcount(&data->g);
	igraph_integer_t n = igraph_vector_int_size(changed);
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_integer_t vid = VECTOR(*changed)[i];
		if (vid >= 0 && vid < (igraph_integer_t)data->node_count) {
			// Color by the community's member-set SimHash, not its volatile
			// representative vertex id, so a community keeps its color across
			// merges/splits instead of thrashing every poll.
			uint64_t h = community_simhash_from_membership(comm, vcount, comm[vid]);
			community_simhash_to_rgb(h, data->nodes[vid].color);
		}
	}
}

// ============================================================================
// Mirror maintained Layered Sphere positions into node positions. Copies all
// of data->nodes[] rather than tracking a touched-vertex subset: cheap
// relative to the layout work itself, and simpler than threading a moved-
// vertex list out of dyn_layered_sphere_on_update (which — see
// graph/dyn_layered_sphere.h — now only repositions the spheres
// touched_levels/community_changed actually flag, not every vertex).
// ============================================================================

static void stream_mirror_layered_sphere_positions(GraphData *data)
{
	for (uint32_t v = 0; v < data->node_count; v++) {
		data->nodes[v].position[0] = (float)MATRIX(data->current_layout, v, 0);
		data->nodes[v].position[1] = (float)MATRIX(data->current_layout, v, 1);
		data->nodes[v].position[2] = (float)MATRIX(data->current_layout, v, 2);
	}
}

// ============================================================================
// Debug report: running V/E streamed totals vs. dyn k-core update volume,
// throttled to stderr so a fast firehose doesn't spam once per frame.
//
// The lifetime totals alone are misleading — they naturally track close to
// V+E because nearly every vertex gets lifted exactly once over the graph's
// life (locality is a per-operation property, not a cumulative one). The
// interval deltas (since the last report) and their ratio are the actual
// locality signal: Δupdates/ΔE close to 1-2 means each new edge is only
// lifting its own endpoints, not the whole graph. max_subcore is the honest
// worst case — the most vertices any single edge insertion ever had to
// visit (see dyn_kcore_max_subcore_size()) — and should stay small and flat
// as V grows, not trend upward, if the maintenance is genuinely local.
// ============================================================================

static void stream_debug_report(GraphStream *gs, const GraphData *data)
{
	if (gs->total_vertices_streamed == 0 && gs->total_edges_streamed == 0)
		return;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
	if (now - gs->last_debug_report_time < GRAPH_STREAM_DEBUG_REPORT_INTERVAL_SEC)
		return;
	gs->last_debug_report_time = now;

	uint64_t dv = gs->total_vertices_streamed - gs->vertices_at_last_report;
	uint64_t de = gs->total_edges_streamed - gs->edges_at_last_report;
	uint64_t du = gs->total_kcore_updates - gs->kcore_updates_at_last_report;
	double ratio = (de > 0) ? (double)du / (double)de : 0.0;
	uint64_t dl_updates = gs->total_leiden_updates - gs->leiden_updates_at_last_report;

	fprintf(stderr, "[graph_stream] streamed V=%llu E=%llu (live: V=%u E=%u) | dyn k-core: updates=%llu (ΔV=%llu ΔE=%llu Δupdates=%llu ratio=%.2f) max_subcore=%d%s | dyn leiden: updates=%llu (Δupdates=%llu) communities=%d frontier(last/max)=%d/%d gamma=%.4f%s\n", (unsigned long long)gs->total_vertices_streamed, (unsigned long long)gs->total_edges_streamed, data->node_count, data->edge_count, (unsigned long long)gs->total_kcore_updates, (unsigned long long)dv, (unsigned long long)de, (unsigned long long)du, ratio, dyn_kcore_max_subcore_size(gs->kcore), gs->kcore ? "" : " (disabled)", (unsigned long long)gs->total_leiden_updates, (unsigned long long)dl_updates, gs->leiden ? dyn_leiden_community_count(gs->leiden) : 0, gs->leiden ? dyn_leiden_last_frontier_size(gs->leiden) : 0, gs->leiden ? dyn_leiden_max_frontier_size(gs->leiden) : 0, gs->leiden ? dyn_leiden_resolution(gs->leiden) : 0.0, gs->leiden ? "" : " (disabled)");

	gs->vertices_at_last_report = gs->total_vertices_streamed;
	gs->edges_at_last_report = gs->total_edges_streamed;
	gs->kcore_updates_at_last_report = gs->total_kcore_updates;
	gs->leiden_updates_at_last_report = gs->total_leiden_updates;
}

bool graph_stream_poll(GraphStream *gs, GraphData *data)
{
	if (!gs || gs->fatal_error)
		return false;

	uint32_t old_node_count = data->node_count;
	uint32_t old_edge_count = data->edge_count;

	igraph_vector_int_t new_edges;
	if (igraph_vector_int_init(&new_edges, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "graph_stream_poll: igraph_vector_int_init failed\n");
		return false;
	}

	int pend_n = 0;
	char *lines[GRAPH_STREAM_LINE_BATCH];
	int total = 0;
	int n;
	while (total < GRAPH_STREAM_MAX_LINES_PER_FRAME && (n = os_stream_reader_poll(gs->reader, lines, GRAPH_STREAM_LINE_BATCH)) > 0) {
		for (int i = 0; i < n; i++) {
			if (total < GRAPH_STREAM_MAX_LINES_PER_FRAME) {
				char *name1, *name2;
				bool hw;
				double w;
				if (parse_ncol_line(lines[i], &name1, &name2, &hw, &w)) {
					igraph_integer_t from_vid, to_vid;
					if (ensure_vertex(gs, data, name1, &from_vid) && ensure_vertex(gs, data, name2, &to_vid)) {
						if (igraph_vector_int_push_back(&new_edges, from_vid) != IGRAPH_SUCCESS || igraph_vector_int_push_back(&new_edges, to_vid) != IGRAPH_SUCCESS) {
							fprintf(stderr, "graph_stream_poll: push_back failed, dropping edge %s-%s\n", name1, name2);
						} else {
							gs->pend_weight[pend_n] = w;
							gs->pend_has_weight[pend_n] = hw;
							pend_n++;
						}
					}
				}
			}
			free(lines[i]);
			total++;
		}
		if (n < GRAPH_STREAM_LINE_BATCH)
			break; // queue drained
	}

	bool changed = false;

	// Batched layout-matrix growth: ONE igraph_matrix_add_rows() call for the
	// whole poll (it is O(V_total) per call, not O(rows added) — see
	// include/graph/stream.h).
	igraph_integer_t num_new_vertices = (igraph_integer_t)data->node_count - (igraph_integer_t)old_node_count;
	if (num_new_vertices > 0 && !gs->fatal_error) {
		if (igraph_matrix_add_rows(&data->current_layout, num_new_vertices) != IGRAPH_SUCCESS) {
			fprintf(stderr, "graph_stream_poll: igraph_matrix_add_rows failed — streaming halted\n");
			gs->fatal_error = true;
		} else {
			for (igraph_integer_t vid = old_node_count; vid < (igraph_integer_t)data->node_count; vid++) {
				int gx = vid % GRAPH_STREAM_GRID_SIDE;
				int gy = (vid / GRAPH_STREAM_GRID_SIDE) % GRAPH_STREAM_GRID_SIDE;
				int gz = vid / (GRAPH_STREAM_GRID_SIDE * GRAPH_STREAM_GRID_SIDE);
				double px = gx * GRAPH_STREAM_GRID_SPACING;
				double py = gy * GRAPH_STREAM_GRID_SPACING;
				double pz = gz * GRAPH_STREAM_GRID_SPACING;
				MATRIX(data->current_layout, vid, 0) = px;
				MATRIX(data->current_layout, vid, 1) = py;
				MATRIX(data->current_layout, vid, 2) = pz;
				data->nodes[vid].position[0] = (float)px;
				data->nodes[vid].position[1] = (float)py;
				data->nodes[vid].position[2] = (float)pz;
			}
			changed = true;
		}
	}

	// Batched edge add: ONE igraph_add_edges() call for the whole poll's batch
	// (it is O(E_total) per call, not O(edges added) — see include/graph/stream.h).
	igraph_integer_t num_new_edges = igraph_vector_int_size(&new_edges) / 2;
	bool edges_in_graph = false;
	if (num_new_edges > 0 && !gs->fatal_error) {
		if (igraph_add_edges(&data->g, &new_edges, NULL) != IGRAPH_SUCCESS) {
			fprintf(stderr, "graph_stream_poll: igraph_add_edges failed for batch of %lld — dropping batch\n", (long long)num_new_edges);
		} else {
			edges_in_graph = true;
			if (ensure_edge_capacity(gs, data, old_edge_count + (uint32_t)num_new_edges)) {
				for (igraph_integer_t k = 0; k < num_new_edges; k++) {
					igraph_integer_t eid = old_edge_count + k;
					igraph_integer_t from_vid = VECTOR(new_edges)[2 * k];
					igraph_integer_t to_vid = VECTOR(new_edges)[2 * k + 1];

					if (gs->pend_has_weight[k] && SETEAN(&data->g, "weight", eid, gs->pend_weight[k]) != IGRAPH_SUCCESS)
						fprintf(stderr, "graph_stream_poll: SETEAN weight failed for edge %lld\n", (long long)eid);

					Edge *e = &data->edges[data->edge_count];
					e->from = (uint32_t)from_vid;
					e->to = (uint32_t)to_vid;
					e->selected = 0.0f;
					e->weight = gs->pend_has_weight[k] ? (float)gs->pend_weight[k] : 0.0f;
					data->edge_count++;

					data->nodes[from_vid].degree++;
					data->nodes[to_vid].degree++;
				}
				data->props.edge_count = (int)data->edge_count;
				changed = true;
			}
		}
	}

	if (num_new_vertices > 0)
		gs->total_vertices_streamed += (uint64_t)num_new_vertices;
	if (edges_in_graph)
		gs->total_edges_streamed += (uint64_t)num_new_edges;

	// Maintain live coreness against the igraph graph (also syncs newly
	// added vertices when the edge batch was dropped), then mirror the
	// changed values into node sizes so structural growth is visible.
	if (gs->kcore) {
		igraph_vector_int_t core_changed;
		bool have_changed = igraph_vector_int_init(&core_changed, 0) == IGRAPH_SUCCESS;
		if (!dyn_kcore_on_edges(gs->kcore, &data->g, edges_in_graph ? &new_edges : NULL, have_changed ? &core_changed : NULL)) {
			fprintf(stderr, "graph_stream_poll: dynamic k-core maintenance failed — disabling\n");
			dyn_kcore_destroy(gs->kcore);
			gs->kcore = NULL;
		} else if (have_changed) {
			gs->total_kcore_updates += (uint64_t)igraph_vector_int_size(&core_changed);
			stream_apply_coreness_sizes(gs, data, &core_changed);
		}
		if (have_changed)
			igraph_vector_int_destroy(&core_changed);
	}

	// Maintain live community membership the same way, then mirror changed
	// vertices into node colors. community_changed survives past this block
	// (unlike core_changed above) — the layered-sphere block below also
	// consumes it, to re-seed a sphere whose community grouping shifted even
	// when nobody's coreness did.
	igraph_vector_int_t community_changed;
	bool have_community_changed = igraph_vector_int_init(&community_changed, 0) == IGRAPH_SUCCESS;
	if (gs->leiden) {
		if (!dyn_leiden_on_edges(gs->leiden, &data->g, edges_in_graph ? &new_edges : NULL, have_community_changed ? &community_changed : NULL)) {
			fprintf(stderr, "graph_stream_poll: dynamic Leiden maintenance failed — disabling\n");
			dyn_leiden_destroy(gs->leiden);
			gs->leiden = NULL;
		} else if (have_community_changed) {
			gs->total_leiden_updates += (uint64_t)igraph_vector_int_size(&community_changed);
			stream_apply_community_colors(gs, data, &community_changed);
		}
	}

	// Maintain the live k-core hierarchy the same way; touched_levels feeds
	// the layered-sphere block below (which sphere(s) need re-seeding),
	// analogous to community_changed above but for coreness/radial change.
	igraph_vector_int_t touched_levels;
	bool have_touched_levels = igraph_vector_int_init(&touched_levels, 0) == IGRAPH_SUCCESS;
	if (gs->core_tree) {
		if (!dyn_core_tree_on_edges(gs->core_tree, &data->g, edges_in_graph ? &new_edges : NULL, have_touched_levels ? &touched_levels : NULL)) {
			fprintf(stderr, "graph_stream_poll: dynamic k-core hierarchy maintenance failed — disabling\n");
			dyn_core_tree_destroy(gs->core_tree);
			gs->core_tree = NULL;
		}
	}

	// Maintain the live Layered Sphere layout, on by default (see
	// "Data/Stream > [x] Live Layered Sphere", user-toggleable): sphere
	// assignment is read directly from the k-core hierarchy tree, and only
	// the spheres touched_levels/community_changed actually flag are cleared
	// and re-seeded (see graph/dyn_layered_sphere.h) — falls back to a no-op
	// this poll if the tree or Leiden are themselves unavailable.
	if (gs->layered_sphere && gs->layered_sphere_enabled) {
		const igraph_integer_t *comm_values = gs->leiden ? dyn_leiden_membership(gs->leiden) : NULL;
		if (gs->core_tree && comm_values) {
			if (!dyn_layered_sphere_on_update(gs->layered_sphere, &data->g, gs->core_tree, have_touched_levels ? &touched_levels : NULL, comm_values, have_community_changed ? &community_changed : NULL, &data->current_layout)) {
				fprintf(stderr, "graph_stream_poll: dynamic Layered Sphere maintenance failed — disabling\n");
				dyn_layered_sphere_destroy(gs->layered_sphere);
				gs->layered_sphere = NULL;
			} else {
				stream_mirror_layered_sphere_positions(data);
				changed = true;
			}
		}
	}
	if (have_touched_levels)
		igraph_vector_int_destroy(&touched_levels);
	if (have_community_changed)
		igraph_vector_int_destroy(&community_changed);

	stream_debug_report(gs, data);

	igraph_vector_int_destroy(&new_edges);
	return changed;
}

bool graph_stream_at_eof(GraphStream *stream)
{
	return !stream || os_stream_reader_at_eof(stream->reader);
}

void graph_stream_destroy(GraphStream *stream)
{
	if (!stream)
		return;

	os_stream_reader_request_stop(stream->reader);
	if (os_stream_reader_at_eof(stream->reader))
		os_stream_reader_destroy(stream->reader);
	else
		fprintf(stderr, "graph_stream_destroy: stdin reader still active — leaving it for the OS to reclaim\n");

	dyn_kcore_destroy(stream->kcore);
	dyn_core_tree_destroy(stream->core_tree);
	dyn_leiden_destroy(stream->leiden);
	dyn_layered_sphere_destroy(stream->layered_sphere);
	name_map_destroy(&stream->names);
	free(stream);
}

const int *graph_stream_coreness(const GraphStream *gs)
{
	return (gs && gs->kcore) ? dyn_kcore_values(gs->kcore) : NULL;
}

const igraph_integer_t *graph_stream_community(const GraphStream *gs)
{
	return (gs && gs->leiden) ? dyn_leiden_membership(gs->leiden) : NULL;
}

// ============================================================================
// "Data/Stream > [ ] Pause" menu toggle
// ============================================================================

void *compute_toggle_stream_pause(ExecutionContext *ctx)
{
	(void)ctx;
	return (void *)(uintptr_t)1;
}

void apply_toggle_stream_pause(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	if (!ctx || !ctx->app_state)
		return;
	ctx->app_state->graph_stream_paused = !ctx->app_state->graph_stream_paused;

	MenuNode *node = menu_find_node_by_command_id(ctx->app_state->app_ctx.menu.root, "toggle_stream_pause");
	if (node) {
		free((void *)node->label);
		node->label = strdup(ctx->app_state->graph_stream_paused ? "[x] Pause" : "[ ] Pause");
		if (node->command) {
			free((void *)node->command->display_name);
			node->command->display_name = strdup(node->label);
		}
	}
}

// ============================================================================
// "Data/Stream > [ ] Live Layered Sphere" menu toggle
// ============================================================================

void *compute_toggle_stream_layered_sphere(ExecutionContext *ctx)
{
	(void)ctx;
	return (void *)(uintptr_t)1;
}

void apply_toggle_stream_layered_sphere(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	if (!ctx || !ctx->app_state || !ctx->app_state->graph_stream)
		return;
	GraphStream *gs = ctx->app_state->graph_stream;
	gs->layered_sphere_enabled = !gs->layered_sphere_enabled;

	MenuNode *node = menu_find_node_by_command_id(ctx->app_state->app_ctx.menu.root, "toggle_stream_layered_sphere");
	if (node) {
		free((void *)node->label);
		node->label = strdup(gs->layered_sphere_enabled ? "[x] Live Layered Sphere" : "[ ] Live Layered Sphere");
		if (node->command) {
			free((void *)node->command->display_name);
			node->command->display_name = strdup(node->label);
		}
	}
}
