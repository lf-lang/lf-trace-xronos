# CMake include for TracePluginCustomCmake.lf
# Sets the variables that were previously set via cmake-args

# Get the repo root (2 levels up from this cmake file)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LF_TRACE_INSTALL "${REPO_ROOT}/install")

# These variables are used by reactor-c's lf_trace.cmake
set(LF_TRACE_PLUGIN "lf-trace-xronos")

# Include the main plugin cmake file
include("${CMAKE_CURRENT_LIST_DIR}/../../plugin.cmake")
