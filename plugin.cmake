# CMake include file for lf-trace-xronos plugin
# This file should be used with the cmake-include target property in Lingua Franca.
#
# Usage in .lf file:
#   target C {
#       cmake-include: ["relative/or/absolute/path/to/plugin.cmake"]
#   };
#
# Build the plugin once to create a reusable install folder:
#   ./build.sh
#   (or ./build.sh --install /custom/path/to/install)
#
# Then, for any LF program build, point to that install directory:
#   cmake ... -DLF_TRACE_INSTALL=/abs/path/to/install
# Or set an environment variable:
#   export LF_TRACE_INSTALL=/abs/path/to/install
# Or place plugin.cmake next to the install directory and it will auto-detect

if(NOT DEFINED LF_TRACE_INSTALL)
  if(DEFINED ENV{LF_TRACE_INSTALL})
    set(LF_TRACE_INSTALL "$ENV{LF_TRACE_INSTALL}")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/install")
    set(LF_TRACE_INSTALL "${CMAKE_CURRENT_SOURCE_DIR}/install")
  elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/install")
    get_filename_component(PLUGIN_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
    set(LF_TRACE_INSTALL "${PLUGIN_DIR}/install")
  else()
    message(FATAL_ERROR
      "LF_TRACE_INSTALL is not set.\n"
      "Build the plugin install directory first by running build.sh.\n"
      "and then set LF_TRACE_INSTALL (CMake -D or environment variable) to the absolute path of the `install` directory before building your LF program.")
  endif()
endif()

get_filename_component(LF_TRACE_INSTALL "${LF_TRACE_INSTALL}" ABSOLUTE)
set(LF_TRACE_LIB_DIR "${LF_TRACE_INSTALL}/lib")
set(LF_TRACE_INC_DIR "${LF_TRACE_INSTALL}/include")

if(NOT EXISTS "${LF_TRACE_LIB_DIR}/liblf-trace-impl.a")
  message(FATAL_ERROR "Expected ${LF_TRACE_LIB_DIR}/liblf-trace-impl.a. Did you run ./build.sh ?")
endif()

if(NOT EXISTS "${LF_TRACE_INC_DIR}")
  message(WARNING "Expected ${LF_TRACE_INC_DIR} to exist (headers).")
endif()

message(STATUS "lf-trace-xronos install: ${LF_TRACE_INSTALL}")
message(STATUS "  include: ${LF_TRACE_INC_DIR}")
message(STATUS "  lib:     ${LF_TRACE_LIB_DIR}")

target_include_directories(${LF_MAIN_TARGET} PRIVATE "${LF_TRACE_INC_DIR}")

# Collect all archives from the install directory and link them.
# This avoids having to list every transitive dependency here.
file(GLOB LF_TRACE_ARCHIVES "${LF_TRACE_LIB_DIR}/*.a")
if(NOT LF_TRACE_ARCHIVES)
  message(FATAL_ERROR "No static libraries found under ${LF_TRACE_LIB_DIR} (*.a).")
endif()

set(LF_TRACE_IMPL_ARCHIVE "${LF_TRACE_LIB_DIR}/liblf-trace-impl.a")
list(REMOVE_ITEM LF_TRACE_ARCHIVES "${LF_TRACE_IMPL_ARCHIVE}")
set(LF_TRACE_ALL_ARCHIVES "${LF_TRACE_IMPL_ARCHIVE}" ${LF_TRACE_ARCHIVES})

if(UNIX AND NOT APPLE)
  # On GNU ld/lld, a link group avoids archive order issues when many static libs depend on each other.
  target_link_libraries(${LF_MAIN_TARGET} PRIVATE "-Wl,--start-group" ${LF_TRACE_ALL_ARCHIVES} "-Wl,--end-group")
else()
  target_link_libraries(${LF_MAIN_TARGET} PRIVATE ${LF_TRACE_ALL_ARCHIVES})
endif()

# Link system libraries that are required
# Note: zlib is required for gRPC compression support
find_library(ZLIB_LIBRARY z)
if(ZLIB_LIBRARY)
    target_link_libraries(${LF_MAIN_TARGET} PRIVATE ${ZLIB_LIBRARY})
    message(STATUS "Linked zlib: ${ZLIB_LIBRARY}")
else()
    message(WARNING "zlib not found. Some gRPC features may not work.")
endif()

# Link C++ standard library (required for C++ code in opentelemetry-cpp)
if(APPLE)
    target_link_libraries(${LF_MAIN_TARGET} PRIVATE "-lc++")
    message(STATUS "Linked C++ standard library (libc++)")
elseif(UNIX)
    target_link_libraries(${LF_MAIN_TARGET} PRIVATE "-lstdc++")
    message(STATUS "Linked C++ standard library (libstdc++)")
endif()

# Link CoreFoundation framework (required by Abseil on macOS for time zone handling)
if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    if(COREFOUNDATION_LIBRARY)
        target_link_libraries(${LF_MAIN_TARGET} PRIVATE ${COREFOUNDATION_LIBRARY})
        message(STATUS "Linked CoreFoundation framework: ${COREFOUNDATION_LIBRARY}")
    else()
        # Fallback to framework linking
        target_link_libraries(${LF_MAIN_TARGET} PRIVATE "-framework CoreFoundation")
        message(STATUS "Linked CoreFoundation framework")
    endif()
endif()

# Link c-ares library (required by gRPC for DNS resolution)
# c-ares might be bundled with gRPC or available as a system library
find_library(CARES_LIBRARY cares)
if(CARES_LIBRARY)
    target_link_libraries(${LF_MAIN_TARGET} PRIVATE ${CARES_LIBRARY})
    message(STATUS "Linked c-ares: ${CARES_LIBRARY}")
else()
    # Try to find it in the install directory
    if(EXISTS "${LF_TRACE_LIB_DIR}/libcares.a")
        target_link_libraries(${LF_MAIN_TARGET} PRIVATE "${LF_TRACE_LIB_DIR}/libcares.a")
        message(STATUS "Linked c-ares from install directory")
    else()
        message(WARNING "c-ares library not found. DNS resolution features may not work.")
    endif()
endif()

# Link pthread (required by gRPC)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(${LF_MAIN_TARGET} PRIVATE Threads::Threads)
    message(STATUS "Linked pthread")
endif()

# Link dl (dynamic loading, required by some libraries)
if(UNIX AND NOT APPLE)
    target_link_libraries(${LF_MAIN_TARGET} PRIVATE dl)
    message(STATUS "Linked dl")
endif()

# Set C++ standard (required for opentelemetry-cpp)
set_target_properties(${LF_MAIN_TARGET} PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)

message(STATUS "lf-trace-xronos plugin configured for target: ${LF_MAIN_TARGET}")
