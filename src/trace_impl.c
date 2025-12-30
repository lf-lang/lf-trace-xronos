#include <stdio.h> // debugging only
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>

#include "trace.h"
#include "trace_types.h"
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
static int trace_only_reactions = 1;  // Default: only trace reaction events (reaction_starts, reaction_ends). Set LF_TRACE_VERBOSE=1 to trace all events.
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
 * @brief Get event type name from event type enum value
 * 
 * Uses the trace_event_names array from trace_types.h in the LF runtime.
 * 
 * @param event_type The event type enum value
 * @return String name of the event type, or "Unknown event" if invalid
 */
static const char* get_event_type_name(int event_type) {
  // Use the trace_event_names array from trace_types.h
  // (trace_types.h is included above, and trace_event_names is static there,
  // so we get our own copy when we include it)
  
  // Check bounds - NUM_EVENT_TYPES is the last enum value
  if (event_type >= 0 && event_type < NUM_EVENT_TYPES) {
    return trace_event_names[event_type];
  }
  
  return "Unknown event";
}

/**
 * @brief Build a comma-separated string of low cardinality attribute names
 * 
 * Creates a string listing all low cardinality attributes.
 * 
 * Examples:
 * - When has_description = 0: "xronos.element_type"
 * - When has_description = 1: "xronos.element_type,xronos.fqn,xronos.name,xronos.container_fqn"
 * 
 * Note: This list does NOT include xronos.schema.low_cardinality_attributes itself
 * 
 * @param has_description Whether description field is available (non-NULL and non-empty)
 * @param has_container_fqn Whether container_fqn was extracted (FQN contains '.')
 * @return Comma-separated string (caller must free) or NULL on failure
 */
static char* build_low_cardinality_attributes_list(int has_description, int has_container_fqn) {
  // Base attributes always present
  const char* base_attrs = "xronos.element_type";
  
  if (!has_description) {
    // Only element_type when no description
    char* result = malloc(strlen(base_attrs) + 1);
    if (!result) {
      return NULL;
    }
    strcpy(result, base_attrs);
    return result;
  }
  
  // With description, include fqn, name, and optionally container_fqn
  size_t total_len = strlen(base_attrs) + strlen(",xronos.fqn,xronos.name");
  if (has_container_fqn) {
    total_len += strlen(",xronos.container_fqn");
  }
  total_len += 1; // null terminator
  
  char* result = malloc(total_len);
  if (!result) {
    return NULL;
  }
  
  strcpy(result, base_attrs);
  strcat(result, ",xronos.fqn");
  strcat(result, ",xronos.name");
  if (has_container_fqn) {
    strcat(result, ",xronos.container_fqn");
  }
  
  return result;
}

// IMPLEMENTATION OF VERSION API *********************************************

const version_t* lf_version_tracing() { return &version; }

// IMPLEMENTATION OF TRACE API ***********************************************

/**
 * @brief Register an object description in the trace object table
 * 
 * Stores object descriptions (reactors, triggers, actions, timers, etc.) in the
 * trace object table so they can be looked up later by pointer.
 * 
 * @param description The object description to register
 */
void lf_tracing_register_trace_event(object_description_t description) {
  lf_platform_mutex_lock(trace_mutex);
  
  // Store the description in the table
  if (trace._lf_trace_object_descriptions_size < TRACE_OBJECT_TABLE_SIZE) {
    trace._lf_trace_object_descriptions[trace._lf_trace_object_descriptions_size] = description;
    trace._lf_trace_object_descriptions_size++;
  }
  
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
  
  // Check if this is a reaction event (reaction_starts or reaction_ends)
  int is_reaction_event = (tr->event_type == reaction_starts || tr->event_type == reaction_ends);
  
  // If trace_only_reactions is enabled, skip non-reaction events
  if (trace_only_reactions && !is_reaction_event) {
    if (tid < 0) {
      lf_platform_mutex_unlock(trace_mutex);
    }
    return;
  }
  
  // Get event type name (will be used as fallback or for non-reaction events)
  const char* event_type_name = get_event_type_name(tr->event_type);
  
  // Find reactor description by matching pointer
  // Note: tr->pointer points to the reactor's self struct, not the reaction itself
  // Reactions are not registered in the object table - only reactors are
  object_description_t* reactor_desc = find_object_description(tr->pointer);
  
  // Determine span name based on event type
  // For reaction events (reaction_starts, reaction_ends), use FQN when available
  // For all other events, use event type name
  const char* span_name = event_type_name;  // Default to event type name
  int has_description = 0;
  int has_container_fqn = 0;
  char* reaction_fqn = NULL;
  
  if (is_reaction_event) {
    // For reaction events, try to build FQN: reactor_name + "." + reaction_number
    // Example: "MyReactor.0", "Parent.Child.1"
    // tr->dst_id contains the reaction number (0, 1, 2, ...)
    if (reactor_desc && reactor_desc->description && reactor_desc->description[0] != '\0') {
      // Get reactor name
      const char* reactor_name = reactor_desc->description;
      
      // Build FQN: reactor_name + "." + reaction_number
      if (tr->dst_id >= 0) {
        // Calculate length: reactor_name + "." + reaction_number (max digits) + null terminator
        // Reaction numbers are typically small, but allocate enough space
        size_t fqn_len = strlen(reactor_name) + 1 + 20 + 1; // reactor + "." + number + null
        reaction_fqn = malloc(fqn_len);
        if (reaction_fqn) {
          snprintf(reaction_fqn, fqn_len, "%s.%d", reactor_name, tr->dst_id);
          span_name = reaction_fqn;  // Use FQN as span name (e.g., "MyReactor.0")
          has_description = 1;
          
          // Check if reactor_name contains '.' to determine if container_fqn exists
          if (strchr(reactor_name, '.') != NULL) {
            has_container_fqn = 1;
          }
        }
      } else {
        // No reaction number, just use reactor name
        span_name = reactor_name;
        has_description = 1;
      }
    }
    // If reactor_desc not found or no description, span_name remains event_type_name
  }
  // For non-reaction events, span_name is already set to event_type_name
  
  // Start a span.
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
  
  // xronos.fqn - Fully Qualified Name (e.g., "MyReactor.0", "Parent.Child.1")
  if (has_description && reaction_fqn) {
    otelc_set_bytes_attr(map, "xronos.fqn", 
                         (const uint8_t*)reaction_fqn, 
                         strlen(reaction_fqn));
    
    // xronos.name - The reaction number as a string (e.g., "0" from "MyReactor.0")
    char reaction_name_str[32];
    snprintf(reaction_name_str, sizeof(reaction_name_str), "%d", tr->dst_id);
    otelc_set_bytes_attr(map, "xronos.name", 
                         (const uint8_t*)reaction_name_str, 
                         strlen(reaction_name_str));
    
    // xronos.container_fqn - The reactor name (e.g., "MyReactor" from "MyReactor.0")
    if (reactor_desc && reactor_desc->description) {
      otelc_set_bytes_attr(map, "xronos.container_fqn", 
                           (const uint8_t*)reactor_desc->description, 
                           strlen(reactor_desc->description));
      has_container_fqn = 1;
    }
  }
  
  // Build and add schema metadata (list of low cardinality attributes)
  // This should be computed before adding high cardinality attributes
  char* low_card_attrs_list = build_low_cardinality_attributes_list(has_description, has_container_fqn);
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
  
  // Free allocated memory
  if (reaction_fqn) {
    free(reaction_fqn);
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

  // Check environment variable to control verbose tracing
  // Default: trace only reaction events (trace_only_reactions = 1)
  // Set LF_TRACE_VERBOSE=1 to trace all events (including non-reaction events)
  const char* verbose_env = getenv("LF_TRACE_VERBOSE");
  if (verbose_env && strcmp(verbose_env, "1") == 0) {
    trace_only_reactions = 0;  // Enable verbose mode: trace all events
  }

  // Create backend
  backend = otel_backend_create(
    "http://localhost:4317",
    "LF",
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
