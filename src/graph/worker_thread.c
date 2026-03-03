#include "graph/worker_thread.h"
#include "graph/wrappers.h"
#include <igraph.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Forward declarations of wrapper functions that will run in worker thread
static void* execute_wrapper_job(WorkerJob* job);

// Thread-local pointer to current job for progress reporting
static _Thread_local WorkerJob* tls_current_job = NULL;

// igraph progress handler callback
static igraph_error_t igraph_progress_handler(const char* message, igraph_real_t percent, void* data) {
    if (tls_current_job) {
        float p = (float)percent / 100.0f;
        atomic_store_explicit(&tls_current_job->progress, p, memory_order_release);
    }
    return IGRAPH_SUCCESS;
}

// Worker thread function
static void* worker_thread_func(void* arg) {
    WorkerThreadContext* context = (WorkerThreadContext*)arg;
    
    // Initialize thread-local RNG
    igraph_rng_t thread_rng;
    igraph_rng_init(&thread_rng, &igraph_rngtype_mt19937);
    igraph_rng_set_default(&thread_rng);
    
    // Set progress handler for this thread
    igraph_set_progress_handler(igraph_progress_handler);
    
    while (context->running) {
        pthread_mutex_lock(&context->queue_mutex);
        
        // Wait for job if queue is empty
        while (context->running && 
               (context->queue_head == context->queue_tail)) {
            pthread_cond_wait(&context->queue_cond, &context->queue_mutex);
        }
        
        if (!context->running) {
            pthread_mutex_unlock(&context->queue_mutex);
            break;
        }
        
        // Get next job from queue
        WorkerJob* job = context->job_queue[context->queue_head];
        context->queue_head = (context->queue_head + 1) % context->max_queue_size;
        context->current_job = job;
        
        pthread_mutex_unlock(&context->queue_mutex);
        
        // Store in TLS for progress reporting
        tls_current_job = job;
        
        // Update job status to RUNNING - using atomic store for non-blocking polling
        atomic_store_explicit(&job->status, JOB_STATUS_RUNNING, memory_order_release);
        atomic_store_explicit(&job->progress, 0.0f, memory_order_release);
        
        // Execute the job WITHOUT holding queue_mutex or job->mutex
        execute_wrapper_job(job);
        
        // Clear TLS
        tls_current_job = NULL;
        
        // Update job status based on outcome if not already set by execute_wrapper_job
        if (atomic_load_explicit(&job->status, memory_order_acquire) == JOB_STATUS_RUNNING) {
            atomic_store_explicit(&job->progress, 1.0f, memory_order_release);
            atomic_store_explicit(&job->status, JOB_STATUS_COMPLETED, memory_order_release);
        }
        
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
int worker_thread_init(WorkerThreadContext* context, int max_queue_size) {
    if (!context || max_queue_size <= 0) {
        return -1;
    }
    
    memset(context, 0, sizeof(WorkerThreadContext));
    
    // Initialize mutex and condition variable
    if (pthread_mutex_init(&context->queue_mutex, NULL) != 0) {
        return -1;
    }
    
    if (pthread_cond_init(&context->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&context->queue_mutex);
        return -1;
    }
    
    // Allocate job queue
    context->job_queue = (WorkerJob**)malloc(sizeof(WorkerJob*) * max_queue_size);
    if (!context->job_queue) {
        pthread_cond_destroy(&context->queue_cond);
        pthread_mutex_destroy(&context->queue_mutex);
        return -1;
    }
    
    memset(context->job_queue, 0, sizeof(WorkerJob*) * max_queue_size);
    context->queue_size = 0;
    context->queue_head = 0;
    context->queue_tail = 0;
    context->max_queue_size = max_queue_size;
    context->running = true;
    context->thread_running = false;
    context->current_job = NULL;
    
    // Create worker thread
    if (pthread_create(&context->thread, NULL, worker_thread_func, context) != 0) {
        free(context->job_queue);
        pthread_cond_destroy(&context->queue_cond);
        pthread_mutex_destroy(&context->queue_mutex);
        return -1;
    }
    
    context->thread_running = true;
    return 0;
}

// Submit a job to worker thread
WorkerJob* worker_thread_submit_job(WorkerThreadContext* context, 
                                   WorkerJobType type, 
                                   ExecutionContext* ctx) {
    if (!context || !ctx) {
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
    WorkerJob* job = (WorkerJob*)malloc(sizeof(WorkerJob));
    if (!job) {
        pthread_mutex_unlock(&context->queue_mutex);
        return NULL;
    }
    
    // Allocate and copy execution context on the heap (avoid dangling stack pointer)
    ExecutionContext* ctx_copy = malloc(sizeof(ExecutionContext));
    if (!ctx_copy) {
        free(job);
        pthread_mutex_unlock(&context->queue_mutex);
        return NULL;
    }
    *ctx_copy = *ctx;  // Shallow copy is fine; pointed-to data outlives job
    
    memset(job, 0, sizeof(WorkerJob));
    job->type = type;
    atomic_init(&job->status, JOB_STATUS_PENDING);
    job->ctx = ctx_copy;  // Store heap-allocated copy
    atomic_init(&job->progress, 0.0f);
    job->result_matrix = NULL;
    
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
    
    printf("[Worker] Submitted job type %d to queue\n", type);
    return job;
}

// Cancel a running job
bool worker_thread_cancel_job(WorkerThreadContext* context, WorkerJob* job) {
    if (!context || !job) {
        return false;
    }
    
    WorkerJobStatus status = atomic_load_explicit(&job->status, memory_order_acquire);
    if (status == JOB_STATUS_RUNNING || status == JOB_STATUS_PENDING) {
        atomic_store_explicit(&job->status, JOB_STATUS_CANCELLED, memory_order_release);
        pthread_mutex_lock(&job->mutex);
        strncpy(job->error_message, "Job cancelled by user", sizeof(job->error_message) - 1);
        pthread_mutex_unlock(&job->mutex);
        
        // TODO: Implement actual cancellation mechanism
        // This would require cooperative cancellation in igraph functions
    }
    
    return true;
}

// Get job status and progress
WorkerJobStatus worker_thread_get_job_status(WorkerJob* job, float* progress) {
    if (!job) {
        return JOB_STATUS_NONE;
    }
    
    // Non-blocking polling using atomics
    WorkerJobStatus status = atomic_load_explicit(&job->status, memory_order_acquire);
    if (progress) {
        *progress = atomic_load_explicit(&job->progress, memory_order_acquire);
    }
    
    return status;
}

// Wait for job completion (blocking)
WorkerJobStatus worker_thread_wait_for_job(WorkerJob* job) {
    if (!job) {
        return JOB_STATUS_FAILED;
    }
    
    // Simple polling wait - could be improved with condition variable
    WorkerJobStatus status;
    do {
        usleep(10000); // 10ms
        status = atomic_load_explicit(&job->status, memory_order_acquire);
    } while (status == JOB_STATUS_PENDING || status == JOB_STATUS_RUNNING);
    
    return status;
}

// Clean up worker thread system
void worker_thread_cleanup(WorkerThreadContext* context) {
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
    for (int i = context->queue_head; i != context->queue_tail; 
         i = (i + 1) % context->max_queue_size) {
        WorkerJob* job = context->job_queue[i];
        if (job) {
            // Free result matrix if present
            if (job->result_matrix) {
                igraph_matrix_destroy(job->result_matrix);
                free(job->result_matrix);
            }
            // Free execution context copy if present
            if (job->ctx) {
                free(job->ctx);
            }
            pthread_mutex_destroy(&job->mutex);
            free(job);
        }
    }
    
    if (context->current_job) {
        // Free result matrix if present
        if (context->current_job->result_matrix) {
            igraph_matrix_destroy(context->current_job->result_matrix);
            free(context->current_job->result_matrix);
        }
        // Free execution context copy if present
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
bool worker_thread_is_busy(WorkerThreadContext* context) {
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
WorkerJob* worker_thread_get_current_job(WorkerThreadContext* context) {
    if (!context) {
        return NULL;
    }
    
    WorkerJob* job = NULL;
    pthread_mutex_lock(&context->queue_mutex);
    job = context->current_job;
    pthread_mutex_unlock(&context->queue_mutex);
    
    return job;
}

// Execute wrapper job based on type
static void* execute_wrapper_job(WorkerJob* job) {
    if (!job || !job->ctx) {
        return NULL;
    }
    
    ExecutionContext* ctx = job->ctx;
    igraph_t* graph = ctx->current_graph;
    igraph_integer_t vcount = igraph_vcount(graph);
    
    // Allocate result matrix for this job
    igraph_matrix_t* result = malloc(sizeof(igraph_matrix_t));
    if (!result) {
        pthread_mutex_lock(&job->mutex);
        strncpy(job->error_message, "Failed to allocate result matrix", sizeof(job->error_message) - 1);
        pthread_mutex_unlock(&job->mutex);
        atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
        return NULL;
    }
    
    if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
        free(result);
        pthread_mutex_lock(&job->mutex);
        strncpy(job->error_message, "Failed to initialize result matrix", sizeof(job->error_message) - 1);
        pthread_mutex_unlock(&job->mutex);
        atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
        return NULL;
    }
    
    // Status is already set to JOB_STATUS_RUNNING by the worker thread loop
    
    igraph_error_t result_code = IGRAPH_SUCCESS;
    
    // Compute the layout directly into result matrix (no apply)
    switch (job->type) {
        case JOB_TYPE_LAYOUT_FR:
            result_code = igraph_layout_fruchterman_reingold_3d(
                graph,
                result,
                1,  /* use_seed */
                300,  /* iterations */
                (igraph_real_t)vcount,  /* start_temp */
                NULL,  /* weights */
                NULL,  /* minx */
                NULL,  /* maxx */
                NULL,  /* miny */
                NULL,  /* maxy */
                NULL,  /* minz */
                NULL   /* maxz */
            );
            break;
            
        case JOB_TYPE_LAYOUT_KK:
            result_code = igraph_layout_kamada_kawai_3d(
                graph,
                result,
                0,  /* use_seed - start from spherical init */
                vcount * 10,  /* maxiter */
                0.0,  /* epsilon */
                (igraph_real_t)vcount,  /* kkconst */
                NULL,  /* weights */
                NULL,  /* minx */
                NULL,  /* maxx */
                NULL,  /* miny */
                NULL,  /* maxy */
                NULL,  /* minz */
                NULL   /* maxz */
            );
            break;
            
        case JOB_TYPE_LAYOUT_DRL: {
            igraph_layout_drl_options_t options;
            igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);
            
            result_code = igraph_layout_drl_3d(
                graph,
                result,
                0,      /* use_seed */
                &options,
                NULL    /* weights */
            );
            break;
        }
            
        case JOB_TYPE_LAYOUT_DH: {
            igraph_integer_t ecount = igraph_ecount(graph);
            igraph_real_t density = (vcount > 1) ? (2.0 * ecount) / ((igraph_real_t)(vcount * (vcount - 1))) : 0.0;
            
            igraph_int_t fineiter = (igraph_int_t)fmax(10.0, (igraph_real_t)log2((igraph_real_t)vcount));
            igraph_real_t coolfact = 0.75;
            igraph_real_t w_dist = 1.0;
            igraph_real_t w_border = 0.5;
            igraph_real_t w_edge_len = density / 10.0;
            igraph_real_t w_edge_cross = 1.0 - sqrt(density);
            igraph_real_t w_node_edge = (1.0 - density) / 5.0;
            
            result_code = igraph_layout_davidson_harel(
                graph,
                result,
                0,      /* use_seed */
                10,     /* maxiter */
                fineiter, /* fineiter */
                coolfact,
                w_dist,
                w_border,
                w_edge_len,
                w_edge_cross,
                w_node_edge
            );
            break;
        }
            
        case JOB_TYPE_LAYOUT_RT: {
            igraph_vector_int_t roots;
            igraph_vector_int_init(&roots, vcount > 0 ? 1 : 0);
            if (vcount > 0) {
                igraph_vector_int_set(&roots, 0, 0);
            }
            
            result_code = igraph_layout_reingold_tilford(
                graph,
                result,
                IGRAPH_ALL,  /* mode */
                vcount > 0 ? &roots : NULL,  /* roots */
                NULL   /* rootlevel */
            );
            
            igraph_vector_int_destroy(&roots);
            
            // Convert 2D to 3D if needed
            if (result_code == IGRAPH_SUCCESS && result->ncol < 3) {
                igraph_matrix_resize(result, vcount, 3);
                for (igraph_integer_t i = 0; i < vcount; i++) {
                    igraph_matrix_set(result, i, 2, 0.0);
                }
            }
            break;
        }
            
        case JOB_TYPE_LAYOUT_SUG: {
            igraph_matrix_list_t routing;
            igraph_matrix_list_init(&routing, 0);
            
            igraph_vector_int_t layers;
            igraph_vector_int_init(&layers, vcount);
            
            result_code = igraph_layout_sugiyama(
                graph,
                result,
                &routing,  /* routing */
                &layers,   /* layers */
                1.0,       /* hgap */
                1.0,       /* vgap */
                100,       /* maxiter */
                NULL       /* weights */
            );
            
            igraph_matrix_list_destroy(&routing);
            igraph_vector_int_destroy(&layers);
            break;
        }
            
        case JOB_TYPE_LAYOUT_MDS: {
            // Compute distances first
            igraph_matrix_t dist_matrix;
            if (igraph_matrix_init(&dist_matrix, vcount, vcount) != IGRAPH_SUCCESS) {
                pthread_mutex_lock(&job->mutex);
                strncpy(job->error_message, "Failed to allocate distance matrix", sizeof(job->error_message) - 1);
                pthread_mutex_unlock(&job->mutex);
                atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
                return NULL;
            }
            
            igraph_vs_t all_vs;
            igraph_vs_all(&all_vs);
            
            igraph_error_t dist_result = igraph_distances_dijkstra(
                graph,
                &dist_matrix,
                all_vs,   /* from */
                all_vs,   /* to */
                NULL,     /* weights */
                IGRAPH_UNDIRECTED  /* mode */
            );
            
            igraph_vs_destroy(&all_vs);
            
            if (dist_result != IGRAPH_SUCCESS) {
                igraph_matrix_destroy(&dist_matrix);
                pthread_mutex_lock(&job->mutex);
                snprintf(job->error_message, sizeof(job->error_message), 
                         "igraph_distances_dijkstra failed with code %d", dist_result);
                pthread_mutex_unlock(&job->mutex);
                atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
                return NULL;
            }
            
            result_code = igraph_layout_mds(
                graph,
                result,
                &dist_matrix,  /* distance matrix */
                3              /* dimension */
            );
            
            igraph_matrix_destroy(&dist_matrix);
            break;
        }
            
        case JOB_TYPE_LAYOUT_UMAP:
            result_code = igraph_layout_umap_3d(
                graph,
                result,
                1,  /* use_seed */
                NULL, /* distances */
                0.1, /* min_dist */
                300, /* iterations */
                0 /* fast_approximate */
            );
            break;
            
        default:
            // Unknown job type
            pthread_mutex_lock(&job->mutex);
            snprintf(job->error_message, sizeof(job->error_message), 
                     "Unknown job type: %d", (int)job->type);
            pthread_mutex_unlock(&job->mutex);
            atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
            break;
    }
    
    // Store result in job if successful
    if (result_code == IGRAPH_SUCCESS) {
        job->result_matrix = result;
        atomic_store_explicit(&job->progress, 1.0f, memory_order_release);
        atomic_store_explicit(&job->status, JOB_STATUS_COMPLETED, memory_order_release);
    } else {
        igraph_matrix_destroy(result);
        free(result);
        pthread_mutex_lock(&job->mutex);
        snprintf(job->error_message, sizeof(job->error_message), "igraph layout failed with error code %d", result_code);
        pthread_mutex_unlock(&job->mutex);
        atomic_store_explicit(&job->status, JOB_STATUS_FAILED, memory_order_release);
    }
    
    return NULL;
}
