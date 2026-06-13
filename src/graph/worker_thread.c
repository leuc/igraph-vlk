#include "graph/worker_thread.h"
#include "graph/command_registry.h"
#include <igraph.h>
#include <igraph_step.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Thread-local pointer to current job for progress reporting
static _Thread_local WorkerJob *tls_current_job = NULL;

// igraph progress handler callback
static igraph_error_t igraph_progress_handler(const char *message, igraph_real_t percent, void *data)
{
	if (tls_current_job) {
		float p = (float)percent / 100.0f;
		atomic_store_explicit(&tls_current_job->progress, p, memory_order_release);
	}
	return IGRAPH_SUCCESS;
}

// igraph status handler callback for displaying messages to HUD
static igraph_error_t igraph_status_handler(const char *message, void *data)
{
	if (tls_current_job && message) {
		fprintf(stderr, "[igraph] %s", message);
		pthread_mutex_lock(&tls_current_job->mutex);
		snprintf(tls_current_job->status_message, sizeof(tls_current_job->status_message), "%s", message);
		pthread_mutex_unlock(&tls_current_job->mutex);
	}
	return IGRAPH_SUCCESS;
}

// igraph step handler callback for real-time layout snapshots
static igraph_error_t worker_step_callback(const void *state, void *data)
{
	(void)data;
	if (!tls_current_job) {
		return IGRAPH_SUCCESS;
	}
	if (!tls_current_job->ctx->running) {
		return IGRAPH_INTERRUPTED;
	}

	const igraph_matrix_t *src = (const igraph_matrix_t *)state;
	pthread_mutex_lock(&tls_current_job->snapshot_mutex);

	if (!tls_current_job->snapshot_initialized) {
		igraph_matrix_init_copy(&tls_current_job->snapshot_matrix, src);
		tls_current_job->snapshot_initialized = true;
	} else {
		igraph_integer_t n = igraph_matrix_nrow(src);
		igraph_integer_t nc = igraph_matrix_ncol(src);
		igraph_matrix_resize(&tls_current_job->snapshot_matrix, n, nc);
		for (igraph_integer_t i = 0; i < n; i++) {
			for (igraph_integer_t j = 0; j < nc; j++) {
				MATRIX(tls_current_job->snapshot_matrix, i, j) = MATRIX(*src, i, j);
			}
		}
	}
	tls_current_job->has_new_snapshot = true;
	pthread_mutex_unlock(&tls_current_job->snapshot_mutex);
	return IGRAPH_SUCCESS;
}

// Poll a real-time layout snapshot from the worker thread (non-blocking)

// Worker thread function
static void *worker_thread_func(void *arg)
{
	WorkerThreadContext *context = (WorkerThreadContext *)arg;

	// Initialize thread-local RNG
	igraph_rng_t thread_rng;
	igraph_rng_init(&thread_rng, &igraph_rngtype_mt19937);
	igraph_rng_set_default(&thread_rng);

	// Set progress handler for this thread
	igraph_set_progress_handler(igraph_progress_handler);
	igraph_set_status_handler(igraph_status_handler);
	igraph_set_step_handler(worker_step_callback);

	while (context->running) {
		pthread_mutex_lock(&context->queue_mutex);

		// Wait for job if queue is empty
		while (context->running && (context->queue_head == context->queue_tail)) {
			pthread_cond_wait(&context->queue_cond, &context->queue_mutex);
		}

		if (!context->running) {
			pthread_mutex_unlock(&context->queue_mutex);
			break;
		}

		// Get next job from queue
		WorkerJob *job = context->job_queue[context->queue_head];
		context->queue_head = (context->queue_head + 1) % context->max_queue_size;
		context->current_job = job;

		pthread_mutex_unlock(&context->queue_mutex);

		// Store in TLS for progress reporting
		tls_current_job = job;

		// Update job status to RUNNING
		atomic_store_explicit(&job->status, JOB_STATUS_RUNNING, memory_order_release);
		atomic_store_explicit(&job->progress, 0.0f, memory_order_release);

		// Execute the job
		if (job->worker_func) {
			clock_gettime(CLOCK_MONOTONIC, &job->start_time);
			job->result_data = job->worker_func(job->ctx->current_graph);
			struct timespec end_time;
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			double elapsed = (end_time.tv_sec - job->start_time.tv_sec) * 1000.0 + (end_time.tv_nsec - job->start_time.tv_nsec) / 1e6;
			atomic_store_explicit(&job->elapsed_ms, elapsed, memory_order_release);
			if (job->result_data) {
				atomic_store_explicit(&job->progress, 1.0f, memory_order_release);
				atomic_store_explicit(&job->status, JOB_STATUS_COMPLETED, memory_order_release);
				if (elapsed < 1000.0)
					fprintf(stderr, "[Worker] Job completed in %.0fms\n", elapsed);
				else
					fprintf(stderr, "[Worker] Job completed in %.1fs\n", elapsed / 1000.0);
			} else {
				atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
				if (elapsed < 1000.0)
					fprintf(stderr, "[Worker] Job failed after %.0fms\n", elapsed);
				else
					fprintf(stderr, "[Worker] Job failed after %.1fs\n", elapsed / 1000.0);
			}
		} else {
			atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
		}

		// Clear TLS
		tls_current_job = NULL;

		// Clear current job
		pthread_mutex_lock(&context->queue_mutex);
		context->current_job = NULL;
		pthread_mutex_unlock(&context->queue_mutex);
	}

	// Clean up thread-local RNG
	igraph_rng_destroy(&thread_rng);

	context->thread_running = false;
	return NULL;
}

// Initialize worker thread system
bool worker_thread_init(WorkerThreadContext *context, int max_queue_size)
{
	if (!context) {
		return false;
	}

	memset(context, 0, sizeof(WorkerThreadContext));

	context->running = false;
	context->thread_running = false;
	context->current_job = NULL;
	context->max_queue_size = max_queue_size;
	context->queue_size = 0;
	context->queue_head = 0;
	context->queue_tail = 0;

	context->job_queue = (WorkerJob **)malloc(sizeof(WorkerJob *) * max_queue_size);
	if (!context->job_queue) {
		return false;
	}

	if (pthread_mutex_init(&context->queue_mutex, NULL) != 0) {
		free(context->job_queue);
		return false;
	}

	if (pthread_cond_init(&context->queue_cond, NULL) != 0) {
		pthread_mutex_destroy(&context->queue_mutex);
		free(context->job_queue);
		return false;
	}

	// Start worker thread
	context->running = true;
	if (pthread_create(&context->thread, NULL, worker_thread_func, context) != 0) {
		pthread_cond_destroy(&context->queue_cond);
		pthread_mutex_destroy(&context->queue_mutex);
		free(context->job_queue);
		return false;
	}

	context->thread_running = true;
	return true;
}

// Submit a job to worker thread
WorkerJob *worker_thread_submit_job(WorkerThreadContext *context, CommandDef *cmd, ExecutionContext *ctx)
{
	if (!context || !cmd || !ctx) {
		return NULL;
	}

	pthread_mutex_lock(&context->queue_mutex);

	// Check if queue is full
	int next_tail = (context->queue_tail + 1) % context->max_queue_size;
	if (next_tail == context->queue_head) {
		pthread_mutex_unlock(&context->queue_mutex);
		fprintf(stderr, "[Worker] Job queue is full\n");
		return NULL;
	}

	// Create new job
	WorkerJob *job = (WorkerJob *)malloc(sizeof(WorkerJob));
	if (!job) {
		pthread_mutex_unlock(&context->queue_mutex);
		return NULL;
	}

	// Allocate and copy execution context on the heap
	ExecutionContext *ctx_copy = malloc(sizeof(ExecutionContext));
	if (!ctx_copy) {
		free(job);
		pthread_mutex_unlock(&context->queue_mutex);
		return NULL;
	}
	*ctx_copy = *ctx;

	memset(job, 0, sizeof(WorkerJob));
	atomic_init(&job->status, JOB_STATUS_PENDING);
	job->ctx = ctx_copy;
	atomic_init(&job->progress, 0.0f);
	atomic_init(&job->elapsed_ms, 0.0);
	job->result_data = NULL;

	// Store dynamic function pointers from CommandDef
	job->worker_func = cmd->worker_func;
	job->apply_func = cmd->apply_func;
	job->free_func = cmd->free_func;

	if (pthread_mutex_init(&job->mutex, NULL) != 0) {
		free(ctx_copy);
		free(job);
		pthread_mutex_unlock(&context->queue_mutex);
		return NULL;
	}

	pthread_mutex_init(&job->snapshot_mutex, NULL);
	job->snapshot_initialized = false;
	job->has_new_snapshot = false;
	igraph_matrix_init(&job->snapshot_matrix, 0, 0);

	// Add job to queue
	context->job_queue[context->queue_tail] = job;
	context->queue_tail = next_tail;
	context->queue_size++;

	pthread_mutex_unlock(&context->queue_mutex);

	// Signal worker thread
	pthread_cond_signal(&context->queue_cond);

	printf("[Worker] Submitted job '%s' to queue\n", cmd->display_name);
	return job;
}

// Get job status and progress
WorkerJobStatus worker_thread_get_job_status(WorkerJob *job, float *progress)
{
	if (!job) {
		return JOB_STATUS_NONE;
	}

	WorkerJobStatus status = atomic_load_explicit(&job->status, memory_order_acquire);
	if (progress) {
		*progress = atomic_load_explicit(&job->progress, memory_order_acquire);
	}

	return status;
}

// Get job status message
const char *worker_thread_get_job_status_message(WorkerJob *job)
{
	if (!job) {
		return "";
	}
	static char msg[256];
	pthread_mutex_lock(&job->mutex);
	snprintf(msg, sizeof(msg), "%s", job->status_message);
	pthread_mutex_unlock(&job->mutex);
	return msg;
}

// Get elapsed job time in milliseconds
double worker_thread_get_job_elapsed_ms(WorkerJob *job)
{
	if (!job)
		return 0.0;
	return atomic_load_explicit(&job->elapsed_ms, memory_order_acquire);
}

// Poll a real-time layout snapshot from the worker thread (non-blocking)
bool worker_thread_poll_snapshot(WorkerJob *job, igraph_matrix_t *out_matrix)
{
	if (!job || !out_matrix) {
		return false;
	}
	pthread_mutex_lock(&job->snapshot_mutex);
	if (job->has_new_snapshot && job->snapshot_initialized) {
		igraph_integer_t n = igraph_matrix_nrow(&job->snapshot_matrix);
		igraph_integer_t nc = igraph_matrix_ncol(&job->snapshot_matrix);
		igraph_matrix_resize(out_matrix, n, nc);
		for (igraph_integer_t i = 0; i < n; i++) {
			for (igraph_integer_t j = 0; j < nc; j++) {
				MATRIX(*out_matrix, i, j) = MATRIX(job->snapshot_matrix, i, j);
			}
		}
		job->has_new_snapshot = false;
		pthread_mutex_unlock(&job->snapshot_mutex);
		return true;
	}
	pthread_mutex_unlock(&job->snapshot_mutex);
	return false;
}

// Free a completed/failed worker job and its resources
void worker_job_free(WorkerThreadContext *context, WorkerJob *job)
{
	if (!job)
		return;

	// Clear from worker context if it's the current job
	if (context && context->current_job == job) {
		context->current_job = NULL;
	}

	pthread_mutex_destroy(&job->snapshot_mutex);
	if (job->snapshot_initialized) {
		igraph_matrix_destroy(&job->snapshot_matrix);
	}
	pthread_mutex_destroy(&job->mutex);
	if (job->ctx) {
		free(job->ctx);
	}
	free(job);
}

// Wait for job completion (blocking)
// Clean up worker thread system
void worker_thread_cleanup(WorkerThreadContext *context)
{
	if (!context) {
		return;
	}

	// Signal thread to stop
	pthread_mutex_lock(&context->queue_mutex);
	context->running = false;
	pthread_cond_signal(&context->queue_cond);
	pthread_mutex_unlock(&context->queue_mutex);

	// Wait for thread to finish
	if (context->thread_running) {
		pthread_join(context->thread, NULL);
		context->thread_running = false;
	}

	// Clean up remaining jobs in queue
	pthread_mutex_lock(&context->queue_mutex);
	for (int i = context->queue_head; i != context->queue_tail; i = (i + 1) % context->max_queue_size) {
		WorkerJob *job = context->job_queue[i];
		if (job) {
			if (job->result_data && job->free_func) {
				job->free_func(job->result_data);
			}
			worker_job_free(context, job);
		}
	}

	if (context->current_job) {
		if (context->current_job->result_data && context->current_job->free_func) {
			context->current_job->free_func(context->current_job->result_data);
		}
		worker_job_free(context, context->current_job);
	}

	free(context->job_queue);
	pthread_mutex_unlock(&context->queue_mutex);

	// Destroy synchronization primitives
	pthread_cond_destroy(&context->queue_cond);
	pthread_mutex_destroy(&context->queue_mutex);
}

// Check if worker thread is busy
// Get current job if any

WorkerJob *worker_thread_get_current_job(void)
{
	return tls_current_job;
}
