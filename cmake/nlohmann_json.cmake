# nlohmann/json: reading and writing settings.json. Header-only, MIT licence.
#
# The release archive (114 KB, headers + CMake files) is pinned by URL and SHA-256, like libmpv.
# The hash is of the file downloaded from the project's GitHub release on 2026-11-27.
include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Provides the target nlohmann_json::nlohmann_json (include path only, nothing to build).
FetchContent_MakeAvailable(nlohmann_json)
