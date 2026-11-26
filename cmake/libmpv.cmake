# libmpv: video playback (FFmpeg inside, hardware decoding, practically every format).
#
# Prebuilt by the mpv-winbuild-cmake project. The archive is pinned by URL and SHA-256, so a
# changed or tampered file fails the build instead of silently giving us a different DLL.
# This build includes GPL parts of FFmpeg, which is why WallFlow is GPLv3 (see LICENSE).
include(FetchContent)

set(WALLFLOW_LIBMPV_RELEASE "20261111")
set(WALLFLOW_LIBMPV_FILE "mpv-dev-x86_64-20261111-git-bcde6b93cc.7z")

FetchContent_Declare(
  libmpv
  URL "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/${WALLFLOW_LIBMPV_RELEASE}/${WALLFLOW_LIBMPV_FILE}"
  URL_HASH SHA256=273c079b11ea5575822f972756f4c9a71149a5c10130353abeb8586738938452
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# The archive has no CMakeLists.txt, so this only downloads and unpacks it.
FetchContent_MakeAvailable(libmpv)

# An IMPORTED target describes a library that CMake does not build: where its DLL, its import
# library and its headers are. Linking to `libmpv` then adds all of that automatically.
add_library(libmpv SHARED IMPORTED GLOBAL)
set_target_properties(libmpv PROPERTIES
  IMPORTED_LOCATION "${libmpv_SOURCE_DIR}/libmpv-2.dll"
  # A MinGW-format import library. MSVC's linker reads it fine (tested with link 14.51).
  IMPORTED_IMPLIB "${libmpv_SOURCE_DIR}/libmpv.dll.a"
  INTERFACE_INCLUDE_DIRECTORIES "${libmpv_SOURCE_DIR}/include"
)

# Windows looks for DLLs next to the .exe first, so every program that uses libmpv gets a copy.
function(wallflow_copy_libmpv target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:libmpv>" "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()
