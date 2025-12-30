// SPDX-FileCopyrightText: Copyright (c) 2025 Xronos Inc.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file otel_backend.c
 * @brief C implementation of OpenTelemetry backend using opentelemetry-c API
 * 
 * This file provides a C equivalent of the C++ OtelTelemetryBackend class.
 * 
 * Note: Batch span processor delay can be configured at compile time by defining
 * BATCH_SPAN_PROCESSOR_SCHEDULE_DELAY_MILLIS (default is 500ms in the C++ version).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#include "opentelemetry_c/opentelemetry_c.h"
#include "otel_backend.h"

/**
 * @brief Generate a random deployment ID (hexadecimal string)
 * 
 * Equivalent to the C++ function get_deployment_id().
 * Generates a 128-bit random ID using time-based seeding.
 * 
 * @return A dynamically allocated string containing the deployment ID (caller must free)
 */
static char* get_deployment_id(void) {
  // Get current time in nanoseconds since epoch
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  
  // Use time as seed for randomness (XOR with time to improve randomness)
  srand((unsigned int)(time_ns ^ (time_ns >> 32)));
  
  // Generate two 64-bit random values
  uint64_t random1 = ((uint64_t)rand() << 32) | rand();
  uint64_t random2 = ((uint64_t)rand() << 32) | rand();
  
  // Convert to hexadecimal string (32 hex digits total)
  char* deployment_id = malloc(33); // 32 hex digits + null terminator
  if (!deployment_id) {
    return NULL;
  }
  
  snprintf(deployment_id, 33, "%016llx%016llx", 
           (unsigned long long)random1, 
           (unsigned long long)random2);
  
  return deployment_id;
}

/**
 * @brief Configure OTLP exporter endpoint and SSL settings via environment variables
 * 
 * Equivalent to the C++ function create_exporter().
 * Sets environment variables that the OpenTelemetry C++ SDK reads to configure
 * the OTLP gRPC exporter.
 * 
 * @param endpoint The OTLP endpoint URL (e.g., "http://localhost:4317" or "https://api.example.com")
 * @return 0 on success, -1 on failure
 */
static int configure_exporter(const char* endpoint) {
  if (!endpoint) {
    return -1;
  }

  // Set the traces endpoint
  if (setenv("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT", endpoint, 1) != 0) {
    return -1;
  }

  // Determine if SSL should be used based on endpoint protocol
  // OpenTelemetry uses "false" for secure (TLS) and "true" for insecure
  // So if endpoint starts with "https://", we want use_ssl_credentials = true, which means insecure = false
  int use_ssl = (strncmp(endpoint, "https://", 8) == 0);
  const char* insecure_value = use_ssl ? "false" : "true";
  
  if (setenv("OTEL_EXPORTER_OTLP_TRACES_INSECURE", insecure_value, 1) != 0) {
    return -1;
  }

  // Configure batch span processor delay if BATCH_SPAN_PROCESSOR_ENABLED is set
  // Note: This requires BATCH_SPAN_PROCESSOR_SCHEDULE_DELAY_MILLIS to be defined at compile time
  // The C++ version uses 500ms, so we set it here if the environment variable isn't already set
  if (getenv("OTEL_BSP_SCHEDULE_DELAY") == NULL) {
    // Note: The actual delay is controlled by BATCH_SPAN_PROCESSOR_SCHEDULE_DELAY_MILLIS compile-time define
    // This is a limitation of the opentelemetry-c API
  }

  return 0;
}

/**
 * @brief Create and initialize an OpenTelemetry backend
 * 
 * Allocates and initializes a new otel_backend_t structure.
 * 
 * @param endpoint The OTLP endpoint URL
 * @param application_name The application/service name
 * @param hostname The hostname
 * @param pid The process ID
 * @return Pointer to initialized backend, or NULL on failure
 */
otel_backend_t* otel_backend_create(const char* endpoint, 
                                    const char* application_name,
                                    const char* hostname,
                                    int64_t pid) {
  otel_backend_t* backend = malloc(sizeof(otel_backend_t));
  if (!backend) {
    return NULL;
  }

  backend->endpoint = endpoint ? strdup(endpoint) : NULL;
  backend->application_name = application_name ? strdup(application_name) : NULL;
  backend->hostname = hostname ? strdup(hostname) : NULL;
  backend->pid = pid;
  backend->initialized = 0;

  if ((endpoint && !backend->endpoint) ||
      (application_name && !backend->application_name) ||
      (hostname && !backend->hostname)) {
    otel_backend_destroy(backend);
    return NULL;
  }

  return backend;
}

/**
 * @brief Initialize the OpenTelemetry backend
 * 
 * Equivalent to OtelTelemetryBackend::initialize().
 * Configures the exporter and initializes the tracer provider.
 * 
 * Note: The opentelemetry-c API has limitations:
 * - Resource attributes are limited to service_name, service_version, 
 *   service_namespace, and service_instance_id
 * - Deployment ID and PID cannot be set directly via the C API
 * - Batch processor delay must be set via BATCH_SPAN_PROCESSOR_SCHEDULE_DELAY_MILLIS compile-time define
 * 
 * @param backend The backend to initialize
 * @return 0 on success, -1 on failure
 */
int otel_backend_initialize(otel_backend_t* backend) {
  if (!backend || backend->initialized) {
    return -1;
  }

  // Configure the exporter endpoint and SSL settings
  if (configure_exporter(backend->endpoint) != 0) {
    return -1;
  }

  // Generate deployment ID (for reference, though it can't be set via C API)
  char* deployment_id = get_deployment_id();
  if (deployment_id) {
    // Note: deployment_id cannot be set via opentelemetry-c API
    // It's generated here for potential future use or logging
    free(deployment_id);
  }

  // Initialize the tracer provider
  // Note: The C API only supports service_name, service_version, service_namespace, service_instance_id
  // We map: application_name -> service_name, hostname -> service_instance_id
  // service_version and service_namespace are set to empty/default values
  otelc_init_tracer_provider(
    backend->application_name ? backend->application_name : "unknown-service",
    "",  // service_version - not available in C API parameters
    "",  // service_namespace - not available in C API parameters  
    backend->hostname ? backend->hostname : "unknown-host"  // service_instance_id
  );

  backend->initialized = 1;
  return 0;
}

/**
 * @brief Destroy the OpenTelemetry backend and cleanup resources
 * 
 * Equivalent to OtelTelemetryBackend::~OtelTelemetryBackend().
 * Cleans up the backend and resets the tracer provider.
 * 
 * Note: The opentelemetry-c API doesn't provide a direct way to set a NoopTracerProvider.
 * The cleanup here frees the backend resources. The tracer provider will remain active
 * until the process exits or otelc_init_tracer_provider() is called again.
 * 
 * @param backend The backend to destroy
 */
void otel_backend_destroy(otel_backend_t* backend) {
  if (!backend) {
    return;
  }

  // Note: opentelemetry-c doesn't expose a way to set NoopTracerProvider
  // The tracer provider will remain active until process exit or re-initialization
  
  free(backend->endpoint);
  free(backend->application_name);
  free(backend->hostname);
  free(backend);
}

