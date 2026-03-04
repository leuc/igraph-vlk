#include "graph/worker_thread.h"
#include "graph/command_registry.h"
#include <igraph.h>
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
			job->result_data = job->worker_func(job->ctx->current_graph);
			if (job->result_data) {
				atomic_store_explicit(&job->progress, 1.0f, memory_order_release);
				atomic_store_explicit(&job->status, JOB_STATUS_COMPLETED, memory_order_release);
			} else {
				atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
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
int worker_thread_init(WorkerThreadContext *context, int max_queue_size)
{
	if (!context) {
		return -1;
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
		return -1;
	}

	if (pthread_mutex_init(&context->queue_mutex, NULL) != 0) {
		free(context->job_queue);
		return -1;
	}

	if (pthread_cond_init(&context->queue_cond, NULL) != 0) {
		pthread_mutex_destroy(&context->queue_mutex);
		free(context->job_queue);
		return -1;
	}

	// Start worker thread
	context->running = true;
	if (pthread_create(&context->thread, NULL, worker_thread_func, context) != 0) {
		pthread_cond_destroy(&context->queue_cond);
		pthread_mutex_destroy(&context->queue_mutex);
		free(context->job_queue);
		return -1;
	}

	context->thread_running = true;
	return 0;
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

// Cancel a running job
bool worker_thread_cancel_job(WorkerThreadContext *context, WorkerJob *job)
{
	if (!context || !job) {
		return false;
	}

	WorkerJobStatus status = atomic_load_explicit(&job->status, memory_order_acquire);
	if (status == JOB_STATUS_RUNNING || status == JOB_STATUS_PENDING) {
		atomic_store_explicit(&job->status, JOB_STATUS_CANCELLED, memory_order_release);
		pthread_mutex_lock(&job->mutex);
		strncpy(job->error_message, "Job cancelled by user", sizeof(job->error_message) - 1);
		pthread_mutex_unlock(&job->mutex);
	}

	return true;
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

// Wait for job completion (blocking)
WorkerJobStatus worker_thread_wait_for_job(WorkerJob *job)
{
	if (!job) {
		return JOB_STATUS_FAILED;
	}

	WorkerJobStatus status;
	do {
		usleep(10000); // 10ms
		status = atomic_load_explicit(&job->status, memory_order_acquire);
	} while (status == JOB_STATUS_PENDING || status == JOB_STATUS_RUNNING);

	return status;
}

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
			if (job->ctx) {
				free(job->ctx);
			}
			pthread_mutex_destroy(&job->mutex);
			free(job);
		}
	}

	if (context->current_job) {
		if (context->current_job->result_data && context->current_job->free_func) {
			context->current_job->free_func(context->current_job->result_data);
		}
		if (context->current_job->ctx) {
			free(context->current_job->ctx);
		}
		pthread_mutex_destroy(&context->current_job->mutex);
		free(context->current_job);
	}

	free(context->job_queue);
	pthread_mutex_unlock(&context->queue_mutex);

	// Destroy synchronization primitives
	pthread_cond_destroy(&context->queue_cond);
	pthread_mutex_destroy(&context->queue_mutex);
}

// Check if worker thread is busy
bool worker_thread_is_busy(WorkerThreadContext *context)
{
	if (!context) {
		return false;
	}

	bool busy = false;
	pthread_mutex_lock(&context->queue_mutex);
	busy = (context->queue_head != context->queue_tail) || (context->current_job != NULL);
	pthread_mutex_unlock(&context->queue_mutex);

	return busy;
}

// Get current job if any
WorkerJob *worker_thread_get_current_job(WorkerThreadContext *context)
{
	if (!context) {
		return NULL;
	}

	WorkerJob *job = NULL;
	pthread_mutex_lock(&context->queue_mutex);
	job = context->current_job;
	pthread_mutex_unlock(&context->queue_mutex);

	return job;
}
