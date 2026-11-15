# SPDX-License-Identifier: Apache-2.0
#
# Bundles Noto Color Emoji, so that an emoji sequence the system's emoji font is too old to know -- an Emoji
# 17.0 ZWJ sequence on a 2024 distribution, say -- still renders as the one glyph it is meant to be, instead
# of as the pieces it is made from. The terminal falls back to it last, after every emoji font of the system
# (see text::BundledEmojiFontLocator): a system font that has the glyph always wins.
#
# The font is installed in an `emoji-font` directory BESIDE the bundled fonts directory, deliberately: that
# directory is registered with fontconfig and CoreText at startup (text::registerBundledFonts), fontconfig
# scans it recursively, and a registered Noto Color Emoji would become the system's own first choice for
# every emoji. Only the locator reaches it (text::bundledEmojiFontPath).
#
# It is the COLRv1 build, which Cairo draws, and Cairo learned COLRv1 in 1.18: against an older Cairo the
# font would draw nothing at all, which is worse than not bundling it.
#
# The font is NOT kept in the repository. It comes from the noto-emoji repository at a pinned tag, pinned by
# SHA-256, and there are three ways to get it, chosen with CONTOUR_EMOJI_FONT:
#
#   FETCH    (default) Download the font and its licence at configure time. Nothing else to do. Degrades to
#            OFF, with a message, against a Cairo older than 1.18.
#   LOCAL    Use the two files in CONTOUR_EMOJI_FONT_LOCAL_DIR (Noto-COLRv1.ttf and LICENSE), which must be
#            the ones the pinned hashes describe. For builds with no network access: Flatpak/Flathub,
#            distribution build roots and the like fetch them as checksummed sources and hand them in:
#
#                -DCONTOUR_EMOJI_FONT=LOCAL -DCONTOUR_EMOJI_FONT_LOCAL_DIR=/path/to/dir
#
#   OFF      Bundle nothing. Emoji then render only with the system's fonts, and a sequence none of them
#            combines is drawn as its pieces, fitted into its cells.
#
# Upgrading is the version and the two hashes below.
#
# Sets, for the directories that include this one:
#   CONTOUR_EMOJI_FONT_DIR    The directory holding the font and its licence, or empty when none is bundled.
#                             A cache variable, so every subdirectory sees it.

set(CONTOUR_EMOJI_FONT "FETCH" CACHE STRING
    "How Noto Color Emoji is bundled: FETCH (download it), LOCAL (use CONTOUR_EMOJI_FONT_LOCAL_DIR) or OFF")
set_property(CACHE CONTOUR_EMOJI_FONT PROPERTY STRINGS FETCH LOCAL OFF)

set(CONTOUR_EMOJI_FONT_VERSION "v2.051" CACHE STRING "The noto-emoji tag the emoji font is taken from")
set(CONTOUR_EMOJI_FONT_SHA256
    "0ae57fe58645638523ba35f388d93739d292539a9acb84df5700c81b1e1a28d2"
    CACHE STRING "SHA-256 of fonts/Noto-COLRv1.ttf at that tag")
set(CONTOUR_EMOJI_FONT_LICENSE_SHA256
    "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"
    CACHE STRING "SHA-256 of fonts/LICENSE at that tag")
set(CONTOUR_EMOJI_FONT_LOCAL_DIR "" CACHE PATH
    "With CONTOUR_EMOJI_FONT=LOCAL: the directory holding Noto-COLRv1.ttf and LICENSE")

set(CONTOUR_EMOJI_FONT_DIR "" CACHE INTERNAL "Where the bundled emoji font was staged")

set(_emoji_font_file "Noto-COLRv1.ttf")
set(_emoji_font_license "LICENSE-NotoColorEmoji.txt")

if(NOT CONTOUR_EMOJI_FONT MATCHES "^(FETCH|LOCAL|OFF)$")
    message(FATAL_ERROR "CONTOUR_EMOJI_FONT is '${CONTOUR_EMOJI_FONT}'; it must be FETCH, LOCAL or OFF.")
endif()

if(CONTOUR_EMOJI_FONT STREQUAL "OFF")
    message(STATUS "Noto Color Emoji: not bundled (CONTOUR_EMOJI_FONT=OFF)")
    return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(_contour_emoji_cairo QUIET cairo)
endif()
if(_contour_emoji_cairo_FOUND AND _contour_emoji_cairo_VERSION VERSION_LESS "1.18")
    if(CONTOUR_EMOJI_FONT STREQUAL "LOCAL")
        message(FATAL_ERROR
            "CONTOUR_EMOJI_FONT=LOCAL, but Cairo ${_contour_emoji_cairo_VERSION} cannot draw COLRv1 fonts "
            "(that needs 1.18). Build with -DCONTOUR_EMOJI_FONT=OFF.")
    endif()
    message(STATUS
        "Noto Color Emoji: not bundled, Cairo ${_contour_emoji_cairo_VERSION} < 1.18 cannot draw COLRv1")
    return()
endif()

set(_emoji_font_stage "${CMAKE_BINARY_DIR}/bundled-fonts/noto-emoji-${CONTOUR_EMOJI_FONT_VERSION}")

# Checks that @p file has the SHA-256 @p expected, failing the configure with @p what named otherwise.
function(_contour_emoji_verify file expected what)
    file(SHA256 "${file}" _actual)
    if(NOT _actual STREQUAL expected)
        message(FATAL_ERROR
            "${file} has SHA-256 ${_actual}, but ${what} of noto-emoji ${CONTOUR_EMOJI_FONT_VERSION} is pinned to "
            "${expected}. Hand in that tag's file, or set CONTOUR_EMOJI_FONT_VERSION and its hashes to the one "
            "you have.")
    endif()
endfunction()

if(NOT EXISTS "${_emoji_font_stage}/${_emoji_font_file}" OR NOT EXISTS "${_emoji_font_stage}/${_emoji_font_license}")
    if(CONTOUR_EMOJI_FONT STREQUAL "FETCH")
        set(_emoji_font_base
            "https://raw.githubusercontent.com/googlefonts/noto-emoji/${CONTOUR_EMOJI_FONT_VERSION}/fonts")
        set(_emoji_font_source "${CMAKE_BINARY_DIR}/_downloads/noto-emoji-${CONTOUR_EMOJI_FONT_VERSION}")
        foreach(_pair "${_emoji_font_file}|${CONTOUR_EMOJI_FONT_SHA256}" "LICENSE|${CONTOUR_EMOJI_FONT_LICENSE_SHA256}")
            string(REPLACE "|" ";" _pair "${_pair}")
            list(GET _pair 0 _name)
            list(GET _pair 1 _hash)
            message(STATUS "Noto Color Emoji: downloading ${_emoji_font_base}/${_name}")
            # EXPECTED_HASH skips the download when the file is already there and matches, and fails the
            # download when it does not: what is bundled is what the hash above names.
            file(DOWNLOAD "${_emoji_font_base}/${_name}" "${_emoji_font_source}/${_name}"
                EXPECTED_HASH "SHA256=${_hash}"
                STATUS _emoji_font_status)
            list(GET _emoji_font_status 0 _emoji_font_code)
            if(NOT _emoji_font_code EQUAL 0)
                file(REMOVE "${_emoji_font_source}/${_name}")
                message(FATAL_ERROR
                    "Could not fetch ${_name} of Noto Color Emoji: ${_emoji_font_status}\n"
                    "Without network access, hand the files in:\n"
                    "  -DCONTOUR_EMOJI_FONT=LOCAL -DCONTOUR_EMOJI_FONT_LOCAL_DIR=/path/to/dir\n"
                    "or build without it: -DCONTOUR_EMOJI_FONT=OFF")
            endif()
        endforeach()
    else()
        set(_emoji_font_source "${CONTOUR_EMOJI_FONT_LOCAL_DIR}")
        if(NOT EXISTS "${_emoji_font_source}/${_emoji_font_file}" OR NOT EXISTS "${_emoji_font_source}/LICENSE")
            message(FATAL_ERROR
                "CONTOUR_EMOJI_FONT=LOCAL needs CONTOUR_EMOJI_FONT_LOCAL_DIR to name a directory holding "
                "${_emoji_font_file} and LICENSE (it is '${CONTOUR_EMOJI_FONT_LOCAL_DIR}').")
        endif()
        _contour_emoji_verify("${_emoji_font_source}/${_emoji_font_file}" "${CONTOUR_EMOJI_FONT_SHA256}" "the font")
        _contour_emoji_verify("${_emoji_font_source}/LICENSE" "${CONTOUR_EMOJI_FONT_LICENSE_SHA256}" "the licence")
    endif()

    file(REMOVE_RECURSE "${_emoji_font_stage}")
    file(MAKE_DIRECTORY "${_emoji_font_stage}")
    file(COPY_FILE "${_emoji_font_source}/${_emoji_font_file}" "${_emoji_font_stage}/${_emoji_font_file}")
    file(COPY_FILE "${_emoji_font_source}/LICENSE" "${_emoji_font_stage}/${_emoji_font_license}")
endif()

set(CONTOUR_EMOJI_FONT_DIR "${_emoji_font_stage}" CACHE INTERNAL "")
message(STATUS "Noto Color Emoji: bundling ${CONTOUR_EMOJI_FONT_VERSION} from ${_emoji_font_stage}")
