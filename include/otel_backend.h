// SPDX-FileCopyrightText: Copyright (c) 2025 Xronos Inc.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef OTEL_BACKEND_H
#define OTEL_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure holding OpenTelemetry backend state
 */
typedef struct otel_backend {
  char* endpoint;              ///< OTLP endpoint URL
  char* application_name;      ///< Application/service name
  char* hostname;             ///< Hostname
  int64_t pid;                ///< Process ID
  int initialized;            ///< Initialization flag (1 if initialized, 0 otherwise)
} otel_backend_t;

/**
 * @brief Create and initialize an OpenTelemetry backend
 * 
 * Allocates and initializes a new otel_backend_t structure.
 * 
 * @param endpoint The OTLP endpoint URL (e.g., "http://localhost:4317" or "https://api.example.com")
 * @param application_name The application/service name
 * @param hostname The hostname
 * @param pid The process ID
 * @return Pointer to initialized backend, or NULL on failure
 */
otel_backend_t* otel_backend_create(const char* endpoint, 
                                    const char* application_name,
                                    const char* hostname,
                                    int64_t pid);

/**
 * @brief Initialize the OpenTelemetry backend
 * 
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
int otel_backend_initialize(otel_backend_t* backend);

/**
 * @brief Destroy the OpenTelemetry backend and cleanup resources
 * 
 * Cleans up the backend and frees all allocated resources.
 * 
 * Note: The opentelemetry-c API doesn't provide a direct way to set a NoopTracerProvider.
 * The cleanup here frees the backend resources. The tracer provider will remain active
 * until the process exits or otelc_init_tracer_provider() is called again.
 * 
 * @param backend The backend to destroy
 */
void otel_backend_destroy(otel_backend_t* backend);

#ifdef __cplusplus
}
#endif

#endif // OTEL_BACKEND_H

