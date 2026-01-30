# CMake include for FederatedTracePluginCustomCmake.lf
# Sets LF_TRACE_INSTALL and includes plugin.cmake from the repo root.
#
# Unlike trace-customcmake.cmake which uses a hardcoded depth, this file
# searches upward from its own location to find the repo root (identified
# by the presence of plugin.cmake). This makes it robust to the deeper
# directory nesting used by federated builds.

set(_search_dir "${CMAKE_CURRENT_LIST_DIR}")
set(_found FALSE)
foreach(_i RANGE 10)
    if(EXISTS "${_search_dir}/plugin.cmake")
        set(_found TRUE)
        break()
    endif()
    get_filename_component(_search_dir "${_search_dir}/.." ABSOLUTE)
endforeach()

if(NOT _found)
    message(FATAL_ERROR "Could not find plugin.cmake by searching upward from ${CMAKE_CURRENT_LIST_DIR}")
endif()

set(LF_TRACE_INSTALL "${_search_dir}/install")
set(LF_TRACE_PLUGIN "lf-trace-xronos")
include("${_search_dir}/plugin.cmake")
