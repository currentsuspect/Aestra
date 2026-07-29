# Verifies that the desktop binary's adjacent font bundle is complete and
# byte-identical to the tracked AestraAssets source fonts.
#
# Required -D args:
#   SOURCE_FONT_ROOT   tracked AestraAssets/fonts directory
#   BUNDLED_FONT_ROOT  deployed directory beside the desktop executable

if(NOT SOURCE_FONT_ROOT OR NOT BUNDLED_FONT_ROOT)
    message(FATAL_ERROR "SOURCE_FONT_ROOT and BUNDLED_FONT_ROOT must be provided")
endif()

# Derive the required set from the tracked tree rather than restating it.
#
# A second hard-coded list here would make the guard blind to the failure it
# exists to catch: a font added to AestraAssets/fonts but not to the deploy list
# in Source/CMakeLists.txt would not ship AND would still pass. The deploy list
# stays explicit (repo convention: no configure-time globbing, which goes stale);
# this runs via `cmake -P` at test time, so a glob here is always current.
file(GLOB_RECURSE required_fonts RELATIVE "${SOURCE_FONT_ROOT}" "${SOURCE_FONT_ROOT}/*.ttf")

# A verifier that iterates nothing must fail, not report success — otherwise a
# wrong SOURCE_FONT_ROOT silently turns this into a no-op that passes.
if(NOT required_fonts)
    message(FATAL_ERROR "No tracked fonts found under ${SOURCE_FONT_ROOT}")
endif()

foreach(font_relative_path IN LISTS required_fonts)
    set(source_font "${SOURCE_FONT_ROOT}/${font_relative_path}")
    set(bundled_font "${BUNDLED_FONT_ROOT}/${font_relative_path}")

    if(NOT EXISTS "${source_font}")
        message(FATAL_ERROR "Tracked source font is missing: ${source_font}")
    endif()
    if(NOT EXISTS "${bundled_font}")
        message(FATAL_ERROR "Desktop font bundle is missing: ${bundled_font}")
    endif()

    file(SHA256 "${source_font}" source_hash)
    file(SHA256 "${bundled_font}" bundled_hash)
    if(NOT source_hash STREQUAL bundled_hash)
        message(FATAL_ERROR "Bundled font differs from tracked source: ${font_relative_path}")
    endif()
endforeach()

message(STATUS "Aestra desktop font bundle is complete and byte-identical")
