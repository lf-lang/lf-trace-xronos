# CMake include for TracePluginSystemPath.lf
# Sets LF_TRACE_INSTALL to the system path where the plugin was installed

set(LF_TRACE_INSTALL "/usr/local")

# Include the main plugin cmake file (3 levels up from src-gen/TracePluginSystemPath/)
include("${CMAKE_CURRENT_LIST_DIR}/../../../plugin.cmake")
