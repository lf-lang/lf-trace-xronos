#include <stdio.h> // debugging only
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "trace.h"
#include "platform.h"
#include "logging_macros.h"
#include "trace_impl.h"

/** Macro to use when access to trace file fails. */
#define _LF_TRACE_FAILURE(trace)                                                                                       \
  do {                                                                                                                 \
    fprintf(stderr, "WARNING: Access to trace file failed.\n");                                                        \
    fclose(trace->_lf_trace_file);                                                                                     \
    trace->_lf_trace_file = NULL;                                                                                      \
    return -1;                                                                                                         \
  } while (0)

// PRIVATE DATA STRUCTURES ***************************************************

static lf_platform_mutex_ptr_t trace_mutex;
static trace_t trace;
static int process_id;
static int64_t start_time;
static version_t version = {.build_config =
                                {
                                    .single_threaded = TRIBOOL_DOES_NOT_MATTER,
#ifdef NDEBUG
                                    .build_type_is_debug = TRIBOOL_FALSE,
#else
                                    .build_type_is_debug = TRIBOOL_TRUE,
#endif
                                    .log_level = LOG_LEVEL,
                                },
                            .core_version_name = NULL};

// PRIVATE HELPERS ***********************************************************


// IMPLEMENTATION OF VERSION API *********************************************

const version_t* lf_version_tracing() { return &version; }

// IMPLEMENTATION OF TRACE API ***********************************************

// FIMXE: This function is to be implemented using Otel Metrics.
void lf_tracing_register_trace_event(object_description_t description) {
  lf_platform_mutex_lock(trace_mutex);

  lf_platform_mutex_unlock(trace_mutex);
}

void lf_tracing_tracepoint(int worker, trace_record_nodeps_t* tr) {
  (void)worker;
  // Worker argument determines which buffer to write to.
  int tid = lf_thread_id();
  if (tid < 0) {
    // The current thread was created by the user. It is not managed by LF, its ID is not known,
    // and most importantly it does not count toward the limit on the total number of threads.
    // Therefore we should fall back to using a mutex.
    lf_platform_mutex_lock(trace_mutex);
  }

  if (tid < 0) {
    lf_platform_mutex_unlock(trace_mutex);
  }
}

void lf_tracing_global_init(char* process_name, char* process_names, int fedid, int max_num_local_threads) {
  (void)process_names;
  trace_mutex = lf_platform_mutex_new();
  if (!trace_mutex) {
    fprintf(stderr, "WARNING: Failed to initialize trace mutex.\n");
    exit(1);
  }
}
void lf_tracing_set_start_time(int64_t time) { start_time = time; }
void lf_tracing_global_shutdown() {
  lf_platform_mutex_free(trace_mutex);
}
