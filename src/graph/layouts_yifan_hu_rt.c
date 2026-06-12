#define _GNU_SOURCE

#include "app_state.h"
#include "graph/worker_thread.h"
#include "graph/wrappers_layout.h"
#include "vulkan/rt_layout.h"
#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// YIFAN HU RT LAYOUT (3D) - Worker
// ============================================================================
void *compute_igraph_layout_yifan_hu_rt_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);

	// Create initial positions (3D grid)
	igraph_matrix_t *init = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(init, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(init);
		return NULL;
	}

	igraph_real_t spacing = 2.0;
	igraph_integer_t side = (igraph_integer_t)ceil(pow(vcount, 1.0 / 3.0));
	igraph_real_t offset = (side * spacing) / 2.0;

	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_integer_t z = i / (side * side);
		igraph_integer_t y = (i % (side * side)) / side;
		igraph_integer_t x = i % side;
		MATRIX(*init, i, 0) = (x * spacing) - offset;
		MATRIX(*init, i, 1) = (y * spacing) - offset;
		MATRIX(*init, i, 2) = (z * spacing) - offset;
	}

	// Access renderer via worker job context
	WorkerJob *job = worker_thread_get_current_job();
	if (!job || !job->ctx || !job->ctx->app_state) {
		igraph_matrix_destroy(init);
		IGRAPH_FREE(init);
		return NULL;
	}
	Renderer *r = &job->ctx->app_state->renderer;

	igraph_int_t maxiter = 500;

	// Initialize GPU resources and start the layout
	if (!yhrt_worker_init(r, graph, init, maxiter)) {
		fprintf(stderr, "[YHRT] Worker init failed\n");
		igraph_matrix_destroy(init);
		IGRAPH_FREE(init);
		return NULL;
	}

	igraph_progress("Yifan Hu (3D) [RT] layout", 0.0, NULL);

	for (igraph_integer_t iter = 0; iter < maxiter; iter++) {
		fprintf(stderr, "[YHRT-WORKER] iter=%d calling step...\n", (int)iter);
		fflush(stderr);
		// Blocking: record, submit, wait on GPU
		if (!yhrt_worker_step(r)) {
			fprintf(stderr, "[YHRT-WORKER] iter=%d step returned false\n", (int)iter);
			fflush(stderr);
			break;
		}

		// Report progress via igraph TLS handler
		double pct = 100.0 * (iter + 1) / maxiter;
		igraph_progress("Yifan Hu (3D) [RT] layout", pct, NULL);

		// Push snapshot every 5 iterations for real-time visual updates
		if ((iter + 1) % 5 == 0 || iter + 1 >= maxiter) {
			fprintf(stderr, "[YHRT-WORKER] iter=%d reading back snapshot...\n", (int)iter);
			fflush(stderr);
			igraph_matrix_t *snap = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
			igraph_matrix_init(snap, vcount, 3);
			if (yhrt_worker_readback(r, snap)) {
				pthread_mutex_lock(&job->snapshot_mutex);
				if (job->snapshot_initialized)
					igraph_matrix_destroy(&job->snapshot_matrix);
				igraph_matrix_init_copy(&job->snapshot_matrix, snap);
				job->snapshot_initialized = true;
				job->has_new_snapshot = true;
				pthread_mutex_unlock(&job->snapshot_mutex);
			}
			igraph_matrix_destroy(snap);
			IGRAPH_FREE(snap);
		}
	}

	igraph_progress("Yifan Hu (3D) [RT] layout", 100.0, NULL);

	// Read back final positions
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	igraph_matrix_init(result, vcount, 3);
	yhrt_worker_readback(r, result);

	// Clean up GPU resources
	yhrt_worker_cleanup(r);

	igraph_matrix_destroy(init);
	IGRAPH_FREE(init);

	return result;
}

// ============================================================================
// YIFAN HU RT LAYOUT - Apply (no-op, GPU work already done on worker)
// ============================================================================
void apply_yhrt_layout(ExecutionContext *ctx, void *result_data)
{
	(void)ctx;
	(void)result_data;
}

// ============================================================================
// YIFAN HU RT LAYOUT - Free
// ============================================================================
void free_yhrt_data(void *result_data)
{
	if (result_data) {
		igraph_matrix_t *mat = (igraph_matrix_t *)result_data;
		igraph_matrix_destroy(mat);
		IGRAPH_FREE(mat);
	}
}
