# CMake include for TracePluginUserPath.lf
# Uses the install directory at the repo root

# Get the repo root (3 levels up from src-gen/TracePluginUserPath/)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(LF_TRACE_INSTALL "${REPO_ROOT}/install")

# Include the main plugin cmake file
include("${CMAKE_CURRENT_LIST_DIR}/../../../plugin.cmake")
