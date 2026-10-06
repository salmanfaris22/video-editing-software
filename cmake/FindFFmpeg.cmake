# FindFFmpeg
# ----------
# Locates the FFmpeg libraries and defines imported targets:
#
#   FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale
#   FFmpeg::swresample FFmpeg::avfilter
#
# pkg-config is preferred (Homebrew, Linux distributions, vcpkg's pkgconf);
# otherwise headers and libraries are searched directly (Windows SDK builds).
#
# Usage: find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil ...)

set(_lectern_ffmpeg_all avformat avcodec avutil swscale swresample avfilter)
if(FFmpeg_FIND_COMPONENTS)
    set(_lectern_ffmpeg_components ${FFmpeg_FIND_COMPONENTS})
else()
    set(_lectern_ffmpeg_components ${_lectern_ffmpeg_all})
endif()

find_package(PkgConfig QUIET)

foreach(_comp IN LISTS _lectern_ffmpeg_components)
    if(TARGET FFmpeg::${_comp})
        set(FFmpeg_${_comp}_FOUND TRUE)
        continue()
    endif()

    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_FFMPEG_${_comp} QUIET IMPORTED_TARGET lib${_comp})
    endif()

    if(PC_FFMPEG_${_comp}_FOUND)
        add_library(FFmpeg::${_comp} INTERFACE IMPORTED)
        target_link_libraries(FFmpeg::${_comp} INTERFACE PkgConfig::PC_FFMPEG_${_comp})
        set(FFmpeg_${_comp}_FOUND TRUE)
        set(FFmpeg_${_comp}_VERSION "${PC_FFMPEG_${_comp}_VERSION}")
    else()
        find_path(FFmpeg_${_comp}_INCLUDE_DIR NAMES lib${_comp}/${_comp}.h)
        find_library(FFmpeg_${_comp}_LIBRARY NAMES ${_comp} lib${_comp})
        if(FFmpeg_${_comp}_INCLUDE_DIR AND FFmpeg_${_comp}_LIBRARY)
            add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                IMPORTED_LOCATION "${FFmpeg_${_comp}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_${_comp}_INCLUDE_DIR}")
            set(FFmpeg_${_comp}_FOUND TRUE)
        endif()
        mark_as_advanced(FFmpeg_${_comp}_INCLUDE_DIR FFmpeg_${_comp}_LIBRARY)
    endif()
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS _lectern_ffmpeg_components
    VERSION_VAR FFmpeg_avcodec_VERSION
    HANDLE_COMPONENTS)
