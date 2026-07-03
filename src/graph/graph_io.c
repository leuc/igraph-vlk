/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "graph/graph_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include <zstd.h>

// ============================================================================
// Internal: common post-read initialization shared by all loaders.
// ============================================================================
static bool graph_finish_load(GraphData *data, const char *node_attr, const char *edge_attr)
{
	(void)edge_attr;
	data->graph_initialized = true;
	data->node_attr_name = node_attr ? strdup(node_attr) : strdup("pagerank");
	data->nodes = NULL;
	data->edges = NULL;
	data->hubs = NULL;
	data->hub_count = 0;

	if (!graph_import_layout_pos(data)) {
		igraph_matrix_init(&data->current_layout, 0, 0);
		igraph_layout_grid_3d(&data->g, &data->current_layout, 0, 0);
	}

	graph_build_visualization(data);
	return true;
}

// ============================================================================
// Internal: detect file format by extension.
// Returns "graphml", "gml", "graphml.zst", "gml.zst", or NULL.
// ============================================================================
static const char *graph_detect_format(const char *filename)
{
	size_t len = strlen(filename);

	// Check compressed suffixes (order matters: longer match first)
	if (len >= 13 && strcasecmp(filename + len - 13, ".graphml.zstd") == 0)
		return "graphml.zst";
	if (len >= 12 && strcasecmp(filename + len - 12, ".graphml.zst") == 0)
		return "graphml.zst";
	if (len >= 9 && strcasecmp(filename + len - 9, ".gml.zstd") == 0)
		return "gml.zst";
	if (len >= 8 && strcasecmp(filename + len - 8, ".gml.zst") == 0)
		return "gml.zst";

	// Uncompressed
	if (len >= 8 && strcasecmp(filename + len - 8, ".graphml") == 0)
		return "graphml";
	if (len >= 4 && strcasecmp(filename + len - 4, ".gml") == 0)
		return "gml";

	return NULL;
}

// ============================================================================
// Internal: decompress a .zst / .zstd file into a malloc'd buffer.
// Returns the buffer (caller must free) and sets *out_size.
// On failure returns NULL.
// ============================================================================
static void *graph_decompress_file(const char *filename, size_t *out_size)
{
	FILE *fp = fopen(filename, "rb");
	if (!fp)
		return NULL;

	fseek(fp, 0, SEEK_END);
	long comp_len = ftell(fp);
	rewind(fp);
	if (comp_len <= 0) {
		fclose(fp);
		return NULL;
	}

	void *comp = malloc((size_t)comp_len);
	if (!comp) {
		fclose(fp);
		return NULL;
	}
	if (fread(comp, 1, (size_t)comp_len, fp) != (size_t)comp_len) {
		free(comp);
		fclose(fp);
		return NULL;
	}
	fclose(fp);

	size_t dst_cap = (size_t)comp_len * 4;
	if (dst_cap < 65536)
		dst_cap = 65536;
	unsigned char *dst = malloc(dst_cap);
	if (!dst) {
		free(comp);
		return NULL;
	}

	ZSTD_DCtx *dctx = ZSTD_createDCtx();
	if (!dctx) {
		free(comp);
		free(dst);
		return NULL;
	}

	ZSTD_inBuffer in = {comp, (size_t)comp_len, 0};
	size_t total_out = 0;

	while (in.pos < in.size) {
		ZSTD_outBuffer out = {(unsigned char *)dst + total_out, dst_cap - total_out, 0};
		size_t ret = ZSTD_decompressStream(dctx, &out, &in);
		if (ZSTD_isError(ret)) {
			ZSTD_freeDCtx(dctx);
			free(comp);
			free(dst);
			fprintf(stderr, "ZSTD decompression failed: %s\n", ZSTD_getErrorName(ret));
			return NULL;
		}
		total_out += out.pos;
		if (ret == 0)
			break;
		if (in.pos < in.size && out.pos == dst_cap - total_out) {
			dst_cap *= 2;
			unsigned char *ndst = realloc(dst, dst_cap);
			if (!ndst) {
				ZSTD_freeDCtx(dctx);
				free(comp);
				free(dst);
				return NULL;
			}
			dst = ndst;
		}
	}

	ZSTD_freeDCtx(dctx);
	free(comp);
	*out_size = total_out;
	return dst;
}

// ============================================================================
// Load a GraphML file.
// ============================================================================
bool graph_load_graphml(const char *filename, GraphData *data, const char *node_attr, const char *edge_attr)
{
	igraph_set_attribute_table(&igraph_cattribute_table);
	FILE *fp = fopen(filename, "r");
	if (!fp)
		return false;
	if (igraph_read_graph_graphml(&data->g, fp, 0) != IGRAPH_SUCCESS) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	return graph_finish_load(data, node_attr, edge_attr);
}

// ============================================================================
// Load a GML file.
// ============================================================================
bool graph_load_gml(const char *filename, GraphData *data, const char *node_attr, const char *edge_attr)
{
	igraph_set_attribute_table(&igraph_cattribute_table);
	FILE *fp = fopen(filename, "r");
	if (!fp)
		return false;
	if (igraph_read_graph_gml(&data->g, fp) != IGRAPH_SUCCESS) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	return graph_finish_load(data, node_attr, edge_attr);
}

// ============================================================================
// Read GML from an open FILE stream into a raw igraph_t.
// ============================================================================
bool graph_read_gml(igraph_t *graph, FILE *fp)
{
	return igraph_read_graph_gml(graph, fp) == IGRAPH_SUCCESS;
}

// ============================================================================
// Auto-detect format by extension and load (supports .zst/.zstd).
// ============================================================================
bool graph_load(const char *filename, GraphData *data, const char *node_attr, const char *edge_attr)
{
	const char *fmt = graph_detect_format(filename);
	if (!fmt) {
		fprintf(stderr, "Unknown graph format (expected .graphml or .gml): %s\n", filename);
		return false;
	}

	// Uncompressed paths — delegate to existing loaders
	if (strcmp(fmt, "graphml") == 0)
		return graph_load_graphml(filename, data, node_attr, edge_attr);
	if (strcmp(fmt, "gml") == 0)
		return graph_load_gml(filename, data, node_attr, edge_attr);

	// Compressed paths — decompress then parse from memory
	igraph_set_attribute_table(&igraph_cattribute_table);

	size_t buf_size = 0;
	void *buf = graph_decompress_file(filename, &buf_size);
	if (!buf)
		return false;

	FILE *mfp = fmemopen(buf, buf_size, "r");
	if (!mfp) {
		free(buf);
		return false;
	}

	bool ok = false;
	if (strcmp(fmt, "graphml.zst") == 0) {
		ok = (igraph_read_graph_graphml(&data->g, mfp, 0) == IGRAPH_SUCCESS);
	} else if (strcmp(fmt, "gml.zst") == 0) {
		ok = (igraph_read_graph_gml(&data->g, mfp) == IGRAPH_SUCCESS);
	}
	fclose(mfp);
	free(buf);

	if (!ok)
		return false;

	return graph_finish_load(data, node_attr, edge_attr);
}
