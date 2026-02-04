## This CMakeLists is for building an LF trace plugin for the Xronos dashboard.

# Ensure this is being built standalone
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    message(FATAL_ERROR "This CMakeLists.txt must be built standalone. It cannot be included from another build.")
endif()

cmake_minimum_required(VERSION 3.13)
project(lf_trace_impl LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# opentelemetry-cpp (and modern dependencies) require at least C++14 (e.g., std::make_unique)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(INCLUDE_OTEL "Include the otel telemetry backend in the build" ON)
if(INCLUDE_OTEL)
  # Ensure all OpenTelemetry libraries are built as static libraries
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
  include(third-party/absl/absl.cmake)
  include(third-party/protobuf/protobuf.cmake)
  include(third-party/grpc/grpc.cmake)
  include(third-party/opentelemetry-cpp/opentelemetry-cpp.cmake)
endif()

# Local LF API targets (standalone build)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lf-api/version)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lf-api/trace)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lf-api/platform)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lf-api/logging)

# Add opentelemetry-c as a subdirectory
# BUILD_SHARED_LIBS is already set to OFF above, so opentelemetry-c will be built as static
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third-party/opentelemetry-c)

add_library(lf-trace-impl STATIC)
add_library(lf::trace-impl ALIAS lf-trace-impl)
#
# NOTE: lf-api targets in this repo are INTERNAL build helpers (header-only interface libs).
# We avoid exporting them as part of the installable package by using include directories
# directly for compiling this plugin.
target_include_directories(lf-trace-impl PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/lf-api/trace
  ${CMAKE_CURRENT_LIST_DIR}/lf-api/trace/types
  ${CMAKE_CURRENT_LIST_DIR}/lf-api/platform
  ${CMAKE_CURRENT_LIST_DIR}/lf-api/logging
  ${CMAKE_CURRENT_LIST_DIR}/lf-api/version
)

target_link_libraries(lf-trace-impl PUBLIC opentelemetry-c::opentelemetry-c)

target_sources(lf-trace-impl PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/trace_impl.c
    ${CMAKE_CURRENT_LIST_DIR}/src/otel_backend.c
)

target_include_directories(lf-trace-impl PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# handle compile-time parameters
if(NOT DEFINED LOG_LEVEL)
    message(FATAL_ERROR "You must set LOG_LEVEL cmake argument")
endif()
target_compile_definitions(lf-trace-impl PRIVATE LOG_LEVEL=${LOG_LEVEL})

# OTEL_ENDPOINT: OpenTelemetry/dashboard endpoint URL
if(NOT DEFINED OTEL_ENDPOINT)
    set(OTEL_ENDPOINT "http://localhost:4317")
endif()
target_compile_definitions(lf-trace-impl PRIVATE OTEL_ENDPOINT="${OTEL_ENDPOINT}")
# build type parameter (release, debug, etc) is implicitly handled by CMake

# make name platform-independent
set_target_properties(lf-trace-impl PROPERTIES PREFIX "")
# NOTE: the output filename MUST be prefixed with `lib`, otherwise `find_library` cannot find it.
set_target_properties(lf-trace-impl PROPERTIES OUTPUT_NAME "liblf-trace-impl")
set_target_properties(lf-trace-impl PROPERTIES SUFFIX ".a")
set_target_properties(lf-trace-impl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/lib")
set_target_properties(lf-trace-impl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_LIST_DIR}/lib")
set_target_properties(lf-trace-impl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/lib")

# -----------------------------------------------------------------------------
# Install + find_package() support (single bundled package)
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS lf-trace-impl
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install opentelemetry-c public headers (trace_impl.c includes opentelemetry_c/opentelemetry_c.h)
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/third-party/opentelemetry-c/include/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Copy all transitive static archives we build into the install prefix.
# This mirrors what build.sh used to do, but via CMake install().
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/cmake/install_deps.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/install_deps.cmake"
  @ONLY
)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install_deps.cmake")

# CMake package config: provides imported target lf::trace-impl that links all archives under <prefix>/lib.
configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/cmake/lf-trace-xronosConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lf-trace-xronosConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/lf-trace-xronos"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/lf-trace-xronosConfigVersion.cmake"
  VERSION "0.1.0"
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/lf-trace-xronosConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/lf-trace-xronosConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/lf-trace-xronos"
)
