#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include "graph/command_registry.h"
#include "interaction/state.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

// Job status
typedef enum { JOB_STATUS_PENDING, JOB_STATUS_RUNNING, JOB_STATUS_COMPLETED, JOB_STATUS_FAILED, JOB_STATUS_CANCELLED, JOB_STATUS_NONE } WorkerJobStatus;

// Job structure
typedef struct
{
	_Atomic WorkerJobStatus status;
	ExecutionContext *ctx;
	void *result_data;
	char error_message[256];
	_Atomic float progress; // 0.0 to 1.0
	pthread_mutex_t mutex;

	// Dynamic job fields
	IgraphWorkerFunc worker_func;
	IgraphApplyFunc apply_func;
	IgraphFreeFunc free_func;
} WorkerJob;

// Worker thread context
typedef struct
{
	pthread_t thread;
	pthread_mutex_t queue_mutex;
	pthread_cond_t queue_cond;
	bool running;
	bool thread_running;
	WorkerJob *current_job;

	// Circular queue for jobs
	WorkerJob **job_queue;
	int queue_size;
	int queue_head;
	int queue_tail;
	int max_queue_size;
} WorkerThreadContext;

// Initialize worker thread system
int worker_thread_init(WorkerThreadContext *context, int max_queue_size);

// Submit a job to worker thread
WorkerJob *worker_thread_submit_job(WorkerThreadContext *context, CommandDef *cmd, ExecutionContext *ctx);

// Cancel a running job
bool worker_thread_cancel_job(WorkerThreadContext *context, WorkerJob *job);

// Get job status and progress
WorkerJobStatus worker_thread_get_job_status(WorkerJob *job, float *progress);

// Wait for job completion (blocking)
WorkerJobStatus worker_thread_wait_for_job(WorkerJob *job);

// Clean up worker thread system
void worker_thread_cleanup(WorkerThreadContext *context);

// Check if worker thread is busy
bool worker_thread_is_busy(WorkerThreadContext *context);

// Get current job if any
WorkerJob *worker_thread_get_current_job(WorkerThreadContext *context);

#endif // WORKER_THREAD_H
