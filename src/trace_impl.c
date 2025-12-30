#include <stdio.h> // debugging only
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>

#include "trace.h"
#include "platform.h"
#include "logging_macros.h"
#include "trace_impl.h"
#include "otel_backend.h"
#include "opentelemetry_c/opentelemetry_c.h"

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
otel_backend_t* backend;
static void* tracer;
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

/**
 * @brief Build a comma-separated string of low cardinality attribute names
 * 
 * Creates a string like "xronos.element_type"
 * Note: This list does NOT include xronos.schema.low_cardinality_attributes itself
 * 
 * @return Comma-separated string (caller must free) or NULL on failure
 */
static char* build_low_cardinality_attributes_list(void) {
  // Only element_type is included since we're not using description field
  const char* attrs = "xronos.element_type";
  
  char* result = malloc(strlen(attrs) + 1);
  if (!result) {
    return NULL;
  }
  
  strcpy(result, attrs);
  return result;
}

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

  // Use the stored tracer (should be initialized in lf_tracing_global_init)
  if (!tracer) {
    tracer = otelc_get_tracer();
  }
  
  if (!tr) {
    if (tid < 0) {
      lf_platform_mutex_unlock(trace_mutex);
    }
    return;
  }
  
  // Use default span name (not using description field)
  const char* span_name = "reaction";
  
  void *span = otelc_start_span(tracer, span_name, OTELC_SPAN_KIND_INTERNAL, "");
  
  // Create attribute map for low cardinality attributes
  void *map = otelc_create_attr_map();
  
  // Add low cardinality attributes
  // xronos.element_type - For reaction spans, element_type is always "reaction"
  const char* element_type_value = "reaction";
  // Use bytes instead of string to avoid UTF-8 validation issues
  otelc_set_bytes_attr(map, "xronos.element_type", 
                       (const uint8_t*)element_type_value, 
                       strlen(element_type_value));
  
  // Build and add schema metadata (list of low cardinality attributes)
  // This should be computed before adding high cardinality attributes
  char* low_card_attrs_list = build_low_cardinality_attributes_list();
  if (low_card_attrs_list) {
    // Use bytes instead of string to avoid UTF-8 validation issues
    otelc_set_bytes_attr(map, "xronos.schema.low_cardinality_attributes", 
                         (const uint8_t*)low_card_attrs_list, 
                         strlen(low_card_attrs_list));
    free(low_card_attrs_list);
  }
  
  // Set low cardinality attributes on span
  otelc_set_span_attrs(span, map);
  otelc_destroy_attr_map(map);
  
  // Now add high cardinality attributes in a separate attribute map
  // High cardinality attributes: timestamp, microstep, lag
  map = otelc_create_attr_map();
  
  // Extract high cardinality data from trace record
  // xronos.timestamp (logical timestamp)
  int64_t timestamp = tr->logical_time;
  otelc_set_int64_t_attr(map, "xronos.timestamp", timestamp);
  
  // xronos.microstep
  int64_t microstep = tr->microstep;
  otelc_set_int64_t_attr(map, "xronos.microstep", microstep);
  
  // xronos.lag (system clock - logical time)
  int64_t lag = tr->physical_time - tr->logical_time;
  otelc_set_int64_t_attr(map, "xronos.lag", lag);
  
  // Set high cardinality attributes on span
  otelc_set_span_attrs(span, map);
  otelc_destroy_attr_map(map);
  
  otelc_end_span(span);

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

  // Create backend
  backend = otel_backend_create(
    "http://localhost:4317",
    "xronos",
    "lf-lang.org",
    getpid()
  );

  // Initialize (configures exporter and tracer provider)
  if (otel_backend_initialize(backend) != 0) {
    // Handle error
  }

  // Get tracer once and store it for reuse
  tracer = otelc_get_tracer();
}

void lf_tracing_set_start_time(int64_t time) { start_time = time; }

void lf_tracing_global_shutdown() {
  // Destroy tracer if it was created
  if (tracer) {
    otelc_destroy_tracer(tracer);
    tracer = NULL;
  }
  
  // Cleanup backend
  otel_backend_destroy(backend);
  lf_platform_mutex_free(trace_mutex);
}
