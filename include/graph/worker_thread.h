#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <pthread.h>
#include <stdbool.h>
#include "interaction/state.h"

// Job types for worker thread
typedef enum {
    JOB_TYPE_LAYOUT_FR,
    JOB_TYPE_LAYOUT_KK,
    JOB_TYPE_LAYOUT_DRL,
    JOB_TYPE_LAYOUT_DH,
    JOB_TYPE_LAYOUT_RT,
    JOB_TYPE_LAYOUT_SUG,
    JOB_TYPE_LAYOUT_UMAP,
    JOB_TYPE_LAYOUT_MDS,
    JOB_TYPE_ANALYSIS_PATH,
    JOB_TYPE_ANALYSIS_CENTRALITY,
    JOB_TYPE_ANALYSIS_COMMUNITY
} WorkerJobType;

// Job status
typedef enum {
    JOB_STATUS_PENDING,
    JOB_STATUS_RUNNING,
    JOB_STATUS_COMPLETED,
    JOB_STATUS_FAILED,
    JOB_STATUS_CANCELLED
} WorkerJobStatus;

// Job structure
typedef struct {
    WorkerJobType type;
    WorkerJobStatus status;
    ExecutionContext* ctx;
    void* result_data;
    igraph_matrix_t* result_matrix;  // Store layout result from worker
    char error_message[256];
    float progress; // 0.0 to 1.0
    pthread_mutex_t mutex;
} WorkerJob;

// Worker thread context
typedef struct {
    pthread_t thread;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    bool running;
    bool thread_running;
    WorkerJob* current_job;
    
    // Circular queue for jobs
    WorkerJob** job_queue;
    int queue_size;
    int queue_head;
    int queue_tail;
    int max_queue_size;
} WorkerThreadContext;

// Initialize worker thread system
int worker_thread_init(WorkerThreadContext* context, int max_queue_size);

// Submit a job to worker thread
WorkerJob* worker_thread_submit_job(WorkerThreadContext* context, 
                                   WorkerJobType type, 
                                   ExecutionContext* ctx);

// Cancel a running job
bool worker_thread_cancel_job(WorkerThreadContext* context, WorkerJob* job);

// Get job status and progress
WorkerJobStatus worker_thread_get_job_status(WorkerJob* job, float* progress);

// Wait for job completion (blocking)
WorkerJobStatus worker_thread_wait_for_job(WorkerJob* job);

// Clean up worker thread system
void worker_thread_cleanup(WorkerThreadContext* context);

// Check if worker thread is busy
bool worker_thread_is_busy(WorkerThreadContext* context);

// Get current job if any
WorkerJob* worker_thread_get_current_job(WorkerThreadContext* context);

#endif // WORKER_THREAD_H
