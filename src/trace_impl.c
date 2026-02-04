#include <stdio.h>
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

#ifndef OTEL_ENDPOINT
#define OTEL_ENDPOINT "http://localhost:4317"
#endif

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

// Thread-local storage for an in-flight reaction span on the current OS thread.
// The LF runtime emits reaction tracepoints as a pair:
// - reaction_starts: immediately before invoking a reaction
// - reaction_ends:   immediately after the reaction returns
// We create the span on reaction_starts and end it on reaction_ends.
#if __STDC_VERSION__ >= 201112L
#define LF_THREAD_LOCAL _Thread_local
#else
#define LF_THREAD_LOCAL __thread
#endif

static LF_THREAD_LOCAL void* active_reaction_span = NULL;
static LF_THREAD_LOCAL void* active_reaction_pointer = NULL;
static LF_THREAD_LOCAL int active_reaction_dst_id = -1;
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
 * @brief Set common high-cardinality attributes on a span.
 *
 * High cardinality attributes: timestamp, microstep, lag.
 */
static void set_common_high_cardinality_attributes(void* span, const trace_record_nodeps_t* tr) {
  if (!span || !tr) {
    return;
  }
  void* map = otelc_create_attr_map();
  otelc_set_int64_t_attr(map, "xronos.timestamp", tr->logical_time);
  otelc_set_uint32_t_attr(map, "xronos.microstep", (uint32_t)tr->microstep);
  otelc_set_int64_t_attr(map, "xronos.lag", tr->physical_time - tr->logical_time);
  otelc_set_span_attrs(span, map);
  otelc_destroy_attr_map(map);
}

/**
 * @brief Add xronos.schema.low_cardinality_attributes to an attribute map.
 */
static void set_low_cardinality_schema_attr(void* map, int has_description, int has_container_fqn) {
  if (!map) {
    return;
  }

  static const char* kLowCardBase[] = {
      "xronos.element_type",
  };
  static const char* kLowCardWithDescNoContainer[] = {
      "xronos.element_type",
      "xronos.fqn",
      "xronos.name",
  };
  static const char* kLowCardWithDescWithContainer[] = {
      "xronos.element_type",
      "xronos.fqn",
      "xronos.name",
      "xronos.container_fqn",
  };

  const char* const* values = kLowCardBase;
  size_t count = sizeof(kLowCardBase) / sizeof(kLowCardBase[0]);
  if (has_description) {
    if (has_container_fqn) {
      values = kLowCardWithDescWithContainer;
      count = sizeof(kLowCardWithDescWithContainer) / sizeof(kLowCardWithDescWithContainer[0]);
    } else {
      values = kLowCardWithDescNoContainer;
      count = sizeof(kLowCardWithDescNoContainer) / sizeof(kLowCardWithDescNoContainer[0]);
    }
  }

  otelc_set_span_of_string_view_attr(map,
                                     "xronos.schema.low_cardinality_attributes",
                                     values,
                                     count);
}

/**
 * @brief Build a reaction FQN as "<reactor_fqn>.<reaction_number>".
 *
 * @return malloc'd string owned by the caller, or NULL if not enough information.
 */
static char* build_reaction_fqn(const object_description_t* reactor_desc, int reaction_number) {
  if (!reactor_desc || !reactor_desc->description || reactor_desc->description[0] == '\0') {
    return NULL;
  }
  if (reaction_number < 0) {
    return NULL;
  }
  const char* reactor_name = reactor_desc->description;
  size_t fqn_len = strlen(reactor_name) + 1 + 20 + 1; // reactor + "." + number + null
  char* reaction_fqn = (char*)malloc(fqn_len);
  if (!reaction_fqn) {
    return NULL;
  }
  snprintf(reaction_fqn, fqn_len, "%s.%d", reactor_name, reaction_number);
  return reaction_fqn;
}

/**
 * @brief Set low-cardinality attributes for a reaction span.
 *
 * Note: We cannot iterate the opaque otelc attribute map to compute the
 * low-cardinality attribute list dynamically, so we compute the expected list
 * based on what we set.
 */
static void set_reaction_low_cardinality_attributes(void* span,
                                                    const char* reaction_fqn,
                                                    int reaction_number,
                                                    const char* reactor_fqn) {
  if (!span) {
    return;
  }

  void* map = otelc_create_attr_map();

  const char* element_type_value = "reaction";
  otelc_set_string_view_attr(map, "xronos.element_type",
                             element_type_value,
                             strlen(element_type_value));

  // We only set xronos.fqn/xronos.name/xronos.container_fqn if we have a reaction_fqn.
  const int has_description = (reaction_fqn != NULL);
  int has_container_fqn = 0;

  if (reaction_fqn) {
    otelc_set_string_view_attr(map, "xronos.fqn",
                               reaction_fqn,
                               strlen(reaction_fqn));

    char reaction_name_str[32];
    snprintf(reaction_name_str, sizeof(reaction_name_str), "%d", reaction_number);
    otelc_set_string_view_attr(map, "xronos.name",
                               reaction_name_str,
                               strlen(reaction_name_str));

    if (reactor_fqn && reactor_fqn[0] != '\0') {
      otelc_set_string_view_attr(map, "xronos.container_fqn",
                                 reactor_fqn,
                                 strlen(reactor_fqn));
      has_container_fqn = 1;
    }
  }

  set_low_cardinality_schema_attr(map, has_description, has_container_fqn);

  otelc_set_span_attrs(span, map);
  otelc_destroy_attr_map(map);
}

/**
 * @brief Set low-cardinality attributes for a generic (non-reaction) trace event span.
 */
static void set_event_low_cardinality_attributes(void* span) {
  if (!span) {
    return;
  }

  void* map = otelc_create_attr_map();
  const char* element_type_value = "trace_event";
  otelc_set_string_view_attr(map, "xronos.element_type",
                             element_type_value,
                             strlen(element_type_value));
  // Only element_type is set.
  set_low_cardinality_schema_attr(map, 0, 0);
  otelc_set_span_attrs(span, map);
  otelc_destroy_attr_map(map);
}

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

  // Fast-path: reaction_ends ends the span that was started on reaction_starts.
  // Do this before any name/attribute computation to avoid unnecessary work.
  if (tr->event_type == reaction_ends) {
    if (active_reaction_span) {
      // Even if mismatched, end to avoid leaking spans.
      otelc_end_span(active_reaction_span);
    }
    active_reaction_span = NULL;
    active_reaction_pointer = NULL;
    active_reaction_dst_id = -1;

    if (tid < 0) {
      lf_platform_mutex_unlock(trace_mutex);
    }
    return;
  }
  
  if (tr->event_type == reaction_starts) {
    // Reaction span start: name it "<reactor_fqn>.<reaction_number>" when possible.
    object_description_t* reactor_desc = find_object_description(tr->pointer);
    char* reaction_fqn = build_reaction_fqn(reactor_desc, tr->dst_id);
    const char* span_name =
        (reaction_fqn != NULL) ? reaction_fqn :
        (reactor_desc && reactor_desc->description && reactor_desc->description[0] != '\0')
            ? reactor_desc->description
            : "reaction";

    void* span = otelc_start_span(tracer, span_name, OTELC_SPAN_KIND_INTERNAL, "");
    set_reaction_low_cardinality_attributes(span,
                                           reaction_fqn,
                                           tr->dst_id,
                                           (reactor_desc ? reactor_desc->description : NULL));
    set_common_high_cardinality_attributes(span, tr);

    // Stash span to be ended by reaction_ends. End any previous active span to avoid leaks.
    if (active_reaction_span) {
      otelc_end_span(active_reaction_span);
    }
    active_reaction_span = span;
    active_reaction_pointer = tr->pointer;
    active_reaction_dst_id = tr->dst_id;

    if (reaction_fqn) {
      free(reaction_fqn);
    }

    if (tid < 0) {
      lf_platform_mutex_unlock(trace_mutex);
    }
    return;
  }

  // Non-reaction event (only emitted if LF_TRACE_VERBOSE=1).
  const char* event_type_name = get_event_type_name(tr->event_type);
  void* span = otelc_start_span(tracer, event_type_name, OTELC_SPAN_KIND_INTERNAL, "");
  set_event_low_cardinality_attributes(span);
  set_common_high_cardinality_attributes(span, tr);
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

  // Check environment variable to control verbose tracing
  // Default: trace only reaction events (trace_only_reactions = 1)
  // Set LF_TRACE_VERBOSE=1 to trace all events (including non-reaction events)
  const char* verbose_env = getenv("LF_TRACE_VERBOSE");
  if (verbose_env && strcmp(verbose_env, "1") == 0) {
    trace_only_reactions = 0;  // Enable verbose mode: trace all events
  }

  // Create backend
  backend = otel_backend_create(
    OTEL_ENDPOINT,
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
