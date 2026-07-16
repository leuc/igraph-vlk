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

#include "os/stream.h"

#define GRAPH_STREAM_LINE_BATCH 256
#define GRAPH_STREAM_MAX_LINES_PER_FRAME 5000
#define GRAPH_STREAM_GRID_SIDE 32
#define GRAPH_STREAM_GRID_SPACING 2.0

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
	uint32_t node_capacity; // amortized capacity of data->nodes[]
	uint32_t edge_capacity; // amortized capacity of data->edges[]
	bool fatal_error;		// set on unrecoverable error; poll() becomes a no-op

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

static bool parse_ncol_line(char *line, char **name1, char **name2, bool *has_weight, double *weight)
{
	if (line[0] == '\0')
		return false; // blank line: silently skipped

	char *saveptr = NULL;
	char *tok1 = strtok_r(line, " \t", &saveptr);
	if (!tok1)
		return false; // whitespace-only line: silently skipped

	char *tok2 = strtok_r(NULL, " \t", &saveptr);
	if (!tok2) {
		fprintf(stderr, "graph_stream: malformed NCOL line (need at least 2 fields): \"%s\"\n", tok1);
		return false;
	}

	const char *tok3 = strtok_r(NULL, " \t", &saveptr);
	double w = 0.0;
	bool hw = false;
	if (tok3) {
		char *end = NULL;
		w = strtod(tok3, &end);
		if (end == tok3 || *end != '\0') {
			fprintf(stderr, "graph_stream: malformed weight field \"%s\", skipping line\n", tok3);
			return false;
		}
		hw = true;
		if (strtok_r(NULL, " \t", &saveptr)) {
			fprintf(stderr, "graph_stream: too many fields in NCOL line \"%s %s ...\", skipping\n", tok1, tok2);
			return false;
		}
	}

	*name1 = tok1;
	*name2 = tok2;
	*has_weight = hw;
	*weight = w;
	return true;
}

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

	gs->reader = os_stream_reader_start();
	if (!gs->reader) {
		fprintf(stderr, "graph_stream_init: failed to start stdin reader thread\n");
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
	if (num_new_edges > 0 && !gs->fatal_error) {
		if (igraph_add_edges(&data->g, &new_edges, NULL) != IGRAPH_SUCCESS) {
			fprintf(stderr, "graph_stream_poll: igraph_add_edges failed for batch of %lld — dropping batch\n", (long long)num_new_edges);
		} else if (ensure_edge_capacity(gs, data, old_edge_count + (uint32_t)num_new_edges)) {
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

	name_map_destroy(&stream->names);
	free(stream);
}
