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
 * @brief Find object description by matching pointer
 * 
 * Searches the trace object descriptions table for an entry matching the given pointer.
 * 
 * @param pointer The pointer to match
 * @return Pointer to matching object_description_t, or NULL if not found
 */
static object_description_t* find_object_description(void* pointer) {
  if (!pointer) {
    return NULL;
  }
  
  for (size_t i = 0; i < trace._lf_trace_object_descriptions_size; i++) {
    if (trace._lf_trace_object_descriptions[i].pointer == pointer) {
      return &trace._lf_trace_object_descriptions[i];
    }
  }
  
  return NULL;
}

/**
 * @brief Extract name from FQN (last component after last '.')
 * 
 * @param fqn The fully qualified name
 * @return Pointer to name within fqn string, or fqn itself if no '.' found
 */
static const char* extract_name_from_fqn(const char* fqn) {
  if (!fqn) {
    return NULL;
  }
  
  const char* last_dot = strrchr(fqn, '.');
  if (last_dot) {
    return last_dot + 1; // Return part after last '.'
  }
  
  return fqn; // No '.', return whole string as name
}

/**
 * @brief Extract container FQN from a full FQN
 * 
 * If FQN contains '.', extracts everything before the last '.'.
 * Otherwise returns NULL (no container).
 * 
 * @param fqn The fully qualified name
 * @return Container FQN (caller must free) or NULL if no container
 */
static char* extract_container_fqn(const char* fqn) {
  if (!fqn) {
    return NULL;
  }
  
  const char* last_dot = strrchr(fqn, '.');
  if (!last_dot) {
    return NULL; // No container
  }
  
  size_t container_len = last_dot - fqn;
  char* container_fqn = malloc(container_len + 1);
  if (!container_fqn) {
    return NULL;
  }
  
  strncpy(container_fqn, fqn, container_len);
  container_fqn[container_len] = '\0';
  return container_fqn;
}

/**
 * @brief Build a comma-separated string of low cardinality attribute names
 * 
 * Creates a string like "xronos.fqn,xronos.name,xronos.element_type,..."
 * Note: This list does NOT include xronos.schema.low_cardinality_attributes itself
 * 
 * @param has_container_fqn Whether container_fqn was added
 * @return Comma-separated string (caller must free) or NULL on failure
 */
static char* build_low_cardinality_attributes_list(int has_container_fqn) {
  // Standard low cardinality attributes for reactions
  const char* base_attrs[] = {
    "xronos.fqn",
    "xronos.name",
    "xronos.element_type"
  };
  
  // Calculate total length needed (including commas between attributes)
  size_t total_len = 0;
  for (size_t i = 0; i < sizeof(base_attrs) / sizeof(base_attrs[0]); i++) {
    total_len += strlen(base_attrs[i]);
    if (i < sizeof(base_attrs) / sizeof(base_attrs[0]) - 1 || has_container_fqn) {
      total_len += 1; // comma
    }
  }
  if (has_container_fqn) {
    total_len += strlen("xronos.container_fqn");
  }
  total_len += 1; // null terminator
  
  char* result = malloc(total_len);
  if (!result) {
    return NULL;
  }
  
  result[0] = '\0';
  
  // Add base attributes
  for (size_t i = 0; i < sizeof(base_attrs) / sizeof(base_attrs[0]); i++) {
    if (i > 0) {
      strcat(result, ",");
    }
    strcat(result, base_attrs[i]);
  }
  
  // Add container_fqn if present
  if (has_container_fqn) {
    strcat(result, ",xronos.container_fqn");
  }
  
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
  
  // Find object description by matching pointer
  object_description_t* desc = find_object_description(tr->pointer);
  
  // Determine span name from description or use default
  const char* span_name = "reaction";
  const char* fqn = desc && desc->description ? desc->description : NULL;
  const char* name = NULL;
  
  if (fqn && fqn[0] != '\0') {
    name = extract_name_from_fqn(fqn);
    if (name && name[0] != '\0') {
      span_name = name;
    }
  }
  
  void *span = otelc_start_span(tracer, span_name, OTELC_SPAN_KIND_INTERNAL, "");
  
  // Create attribute map for low cardinality attributes first
  void *map = otelc_create_attr_map();
  int has_container_fqn = 0;
  
  // Add low cardinality attributes
  if (desc) {
    // xronos.fqn (from description field)
    if (fqn && fqn[0] != '\0') {
      otelc_set_str_attr(map, "xronos.fqn", fqn);
    }
    
    // xronos.name (extracted from FQN)
    if (name && name[0] != '\0') {
      otelc_set_str_attr(map, "xronos.name", name);
    }
    
    // xronos.element_type
    // For reaction spans, element_type is always "reaction"
    otelc_set_str_attr(map, "xronos.element_type", "reaction");
    
    // xronos.container_fqn (only if FQN contains '.')
    if (fqn && fqn[0] != '\0') {
      char* container_fqn = extract_container_fqn(fqn);
      if (container_fqn) {
        otelc_set_str_attr(map, "xronos.container_fqn", container_fqn);
        has_container_fqn = 1;
        free(container_fqn);
      }
    }
  }
  
  // Build and add schema metadata (list of low cardinality attributes)
  // This should be computed before adding high cardinality attributes
  char* low_card_attrs_list = build_low_cardinality_attributes_list(has_container_fqn);
  if (low_card_attrs_list) {
    otelc_set_str_attr(map, "xronos.schema.low_cardinality_attributes", low_card_attrs_list);
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
    "0.0.0.0:4317",
    "lf-trace-xronos",
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
