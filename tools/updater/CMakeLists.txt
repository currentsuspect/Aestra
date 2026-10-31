# AestraUpdater: the small program that swaps the installed Aestra for a verified new copy (see AestraUpdater.cpp).
# Standard library and OS calls only: it has to keep working while everything else is being replaced.
cmake_minimum_required(VERSION 3.22)
add_executable(AestraUpdater AestraUpdater.cpp UpdateApply.h)
target_compile_features(AestraUpdater PRIVATE cxx_std_17)
set_target_properties(AestraUpdater PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    # No console window flashing up for a program the user did not start.
    if(MSVC)
        target_link_options(AestraUpdater PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
endif()
