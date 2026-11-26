#include "wallflow/version.hpp"

namespace wallflow {

std::wstring_view version() noexcept {
    // L"" followed by a plain literal joins into one wide literal at compile time.
    return L"" WALLFLOW_VERSION;
}

}  // namespace wallflow
