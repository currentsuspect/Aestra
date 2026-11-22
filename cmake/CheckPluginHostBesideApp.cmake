# Post-build check: the plugin helper must sit in the same directory as the Aestra binary
# (PluginHostStatus.cpp looks there). Warns loudly rather than failing the build.
get_filename_component(_app_dir "${AESTRA_EXE}" DIRECTORY)
get_filename_component(_helper_dir "${HELPER}" DIRECTORY)
if(NOT EXISTS "${HELPER}")
    message(WARNING "AestraPluginHost was not built: VST3/CLAP plugins will not load")
elseif(NOT "${_app_dir}" STREQUAL "${_helper_dir}")
    message(WARNING "AestraPluginHost is in ${_helper_dir}, not beside Aestra in ${_app_dir}: VST3/CLAP plugins will not load unless AESTRA_PLUGIN_HOST_PATH is set")
endif()
