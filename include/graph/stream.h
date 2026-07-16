/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_STREAM_H
#define GRAPH_STREAM_H

#include "graph_types.h"
#include "interaction/state.h"
#include <stdbool.h>

/* ============================================================================
 * NCOL streaming from stdin.
 *
 * Reads "<name1> <name2> [<weight>]" lines from stdin (via os/stream.h) and
 * incrementally grows a GraphData in place as lines arrive, without the
 * full-rebuild cost of graph_build_visualization()/graph_rebuild_edges().
 *
 * Threading contract: everything in GraphStream that touches igraph_t or
 * GraphData must only ever be called from one thread (the main/render
 * thread) via graph_stream_poll(). The stdin reader itself runs on its own
 * background thread and never touches igraph state directly.
 * ============================================================================ */

typedef struct GraphStream GraphStream;

/**
 * Reset *data* to a fresh, empty, undirected graph and start the background
 * stdin reader. Use INSTEAD OF graph_load()/graph_finish_load() for the
 * streaming case — deliberately skips graph_build_visualization()'s full
 * rescan, which would be far too costly to run every frame.
 * @param data Pointer to GraphData to initialize for streaming
 * @return New GraphStream handle, or NULL on failure (data left in a state
 *         safe to pass to graph_free_data())
 */
GraphStream *graph_stream_init(GraphData *data);

/**
 * Non-blocking. Drains newly-arrived NCOL lines and applies them to *data*
 * (batched igraph_add_vertices/igraph_add_edges/current_layout growth — see
 * src/graph/stream.c for why batching per call, not per line, is required).
 * Call once per frame from the main thread only.
 * @return true if data changed this call (caller should then call
 *         renderer_update_graph() once)
 */
bool graph_stream_poll(GraphStream *gs, GraphData *data);

/**
 * True once the underlying stdin reader has hit EOF and its queue is fully
 * drained — i.e. streaming has permanently stopped.
 */
bool graph_stream_at_eof(GraphStream *stream);

/**
 * Free the GraphStream (reader handle, name index). Does NOT touch
 * GraphData — call graph_free_data() separately as with any other GraphData.
 */
void graph_stream_destroy(GraphStream *stream);

/**
 * Toggle-command pair for the "Data/Stream > [ ] Pause" menu item — flips
 * AppState.graph_stream_paused and updates the menu label to reflect state,
 * mirroring the Layout/Seed toggle in src/graph/layouts_seed.c.
 */
void *compute_toggle_stream_pause(ExecutionContext *ctx);
void apply_toggle_stream_pause(ExecutionContext *ctx, void *result_data);

#endif // GRAPH_STREAM_H
