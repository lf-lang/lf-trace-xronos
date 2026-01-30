# CMake include for TracePluginUserPath.lf
# Uses the install directory at the repo root

# Get the repo root (2 levels up from this cmake file)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LF_TRACE_INSTALL "${REPO_ROOT}/install")

# Include the main plugin cmake file
include("${CMAKE_CURRENT_LIST_DIR}/../../plugin.cmake")
