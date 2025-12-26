## This CMakeLists is for standalone builds only.
## It uses FetchContent to fetch the reactor-c repository and its dependencies.
##
## The dependency targets (trace-api, platform-api, logging-api, version-api) are
## lightweight INTERFACE libraries that are fetched and built as part of this project.

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

# Use FetchContent to get reactor-c repository
# Set policy to allow FetchContent_Populate (we only need source, not build)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

option(INCLUDE_OTEL "Include the otel telemetry backend in the build" ON)
if(INCLUDE_OTEL)
  include(third-party/absl/absl.cmake)
  include(third-party/protobuf/protobuf.cmake)
  include(third-party/grpc/grpc.cmake)
  include(third-party/opentelemetry-cpp/opentelemetry-cpp.cmake)
endif()

# Prevent FetchContent from updating content automatically
# This ensures the repository is only fetched once and cached in the build directory
# Set this as a cache variable so it persists across CMake runs
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Disable automatic updates of FetchContent dependencies")

include(FetchContent)
FetchContent_Declare(
    reactor-c
    GIT_REPOSITORY https://github.com/lf-lang/reactor-c.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)

# Check if already populated before fetching
# FetchContent automatically caches in ${CMAKE_BINARY_DIR}/_deps/reactor-c-src
FetchContent_GetProperties(reactor-c)
if(NOT reactor-c_POPULATED)
    message(STATUS "Fetching reactor-c repository (this will be cached)...")
    # Use FetchContent_Populate instead of MakeAvailable to only fetch source without building
    FetchContent_Populate(reactor-c)
    message(STATUS "reactor-c repository fetched and cached")
else()
    message(STATUS "Using cached reactor-c repository")
endif()

# Get the source directory from FetchContent
# FetchContent_Populate sets reactor-c_SOURCE_DIR directly
set(reactor_c_SOURCE_DIR ${reactor-c_SOURCE_DIR})
if(NOT reactor_c_SOURCE_DIR)
    message(FATAL_ERROR "Failed to fetch reactor-c repository. Check your network connection and SSH access to github.com")
endif()
# Set LF_ROOT to the fetched reactor-c directory
set(LF_ROOT ${reactor_c_SOURCE_DIR})
include(${LF_ROOT}/core/lf_utils.cmake)

# Ensure dependency targets exist
if(NOT TARGET lf::trace-api)
    include(${LF_ROOT}/trace/api/CMakeLists.txt)
endif()
if(NOT TARGET lf::platform-api)
    include(${LF_ROOT}/platform/api/CMakeLists.txt)
endif()
if(NOT TARGET lf::logging-api)
    include(${LF_ROOT}/logging/api/CMakeLists.txt)
endif()
if(NOT TARGET lf::version-api)
    include(${LF_ROOT}/version/api/CMakeLists.txt)
endif()

# Add opentelemetry-c as a subdirectory
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third-party/opentelemetry-c)

add_library(lf-trace-impl STATIC)
add_library(lf::trace-impl ALIAS lf-trace-impl)
target_link_libraries(lf-trace-impl PRIVATE lf::trace-api)
target_link_libraries(lf-trace-impl PRIVATE lf::platform-api)
target_link_libraries(lf-trace-impl PRIVATE lf::logging-api)
target_link_libraries(lf-trace-impl PRIVATE lf::version-api)
target_link_libraries(lf-trace-impl PRIVATE opentelemetry-c)
lf_enable_compiler_warnings(lf-trace-impl)

target_sources(lf-trace-impl PUBLIC 
    ${CMAKE_CURRENT_LIST_DIR}/src/trace_impl.c
    ${CMAKE_CURRENT_LIST_DIR}/src/otel_backend.c
)

target_include_directories(lf-trace-impl PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# handle compile-time parameters
if(NOT DEFINED LOG_LEVEL)
    message(FATAL_ERROR "You must set LOG_LEVEL cmake argument")
endif()
target_compile_definitions(lf-trace-impl PRIVATE LOG_LEVEL=${LOG_LEVEL})
# build type parameter (release, debug, etc) is implicitly handled by CMake

# make name platform-independent
set_target_properties(lf-trace-impl PROPERTIES PREFIX "")
# NOTE: the output filename MUST be prefixed with `lib`, otherwise `find_library` cannot find it.
set_target_properties(lf-trace-impl PROPERTIES OUTPUT_NAME "liblf-trace-impl")
set_target_properties(lf-trace-impl PROPERTIES SUFFIX ".a")
set_target_properties(lf-trace-impl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/lib")
set_target_properties(lf-trace-impl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_LIST_DIR}/lib")
set_target_properties(lf-trace-impl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/lib")
