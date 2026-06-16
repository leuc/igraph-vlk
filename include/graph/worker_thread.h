/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include "graph/command_registry.h"
#include "interaction/state.h"
#include <igraph.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <time.h>

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
	char status_message[256];
	pthread_mutex_t mutex;
	pthread_mutex_t snapshot_mutex;
	igraph_matrix_t snapshot_matrix;
	bool snapshot_initialized;
	bool has_new_snapshot;

	// Dynamic job fields
	IgraphWorkerFunc worker_func;
	IgraphApplyFunc apply_func;
	IgraphFreeFunc free_func;

	// Timing
	struct timespec start_time; // set when job begins executing
	_Atomic double elapsed_ms;	// wall-clock duration in milliseconds (written on completion)
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
bool worker_thread_init(WorkerThreadContext *context, int max_queue_size);

// Submit a job to worker thread
WorkerJob *worker_thread_submit_job(WorkerThreadContext *context, CommandDef *cmd, ExecutionContext *ctx);

// Get job status and progress
WorkerJobStatus worker_thread_get_job_status(WorkerJob *job, float *progress);

// Get job status message
const char *worker_thread_get_job_status_message(WorkerJob *job);

// Get elapsed job time in milliseconds (0 if not yet started)
double worker_thread_get_job_elapsed_ms(WorkerJob *job);

// Poll a new real-time snapshot from the worker thread (non-blocking)
// Returns true if a new snapshot was available and copied into out_matrix
bool worker_thread_poll_snapshot(WorkerJob *job, igraph_matrix_t *out_matrix);

// Clean up worker thread system
void worker_thread_cleanup(WorkerThreadContext *context);

// Free a completed/failed worker job and its resources
void worker_job_free(WorkerThreadContext *context, WorkerJob *job);

#endif // WORKER_THREAD_H
