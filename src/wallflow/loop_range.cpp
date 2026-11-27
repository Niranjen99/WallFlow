#include "wallflow/loop_range.hpp"

#include <cmath>
#include <cwctype>

namespace wallflow {

LoopRange resolve_loop(const LoopSetting& setting, std::optional<double> duration) {
    const LoopRange whole{};

    double start = setting.start.value_or(0);
    if (!std::isfinite(start) || start < 0) {
        start = 0;
    }
    std::optional<double> end = setting.end;
    if (end && !std::isfinite(*end)) {
        end.reset();
    }

    if (duration) {
        if (start >= *duration) {
            return whole;
        }
        if (end && *end >= *duration) {
            end.reset();  // "to the end of the file" is what mpv does by itself
        }
    }

    // The loop must still be long enough, wherever it ends.
    const std::optional<double> loop_end = end ? end : duration;
    if (loop_end && *loop_end - start < kMinLoopSeconds) {
        return whole;
    }
    return {start, end};
}

std::optional<double> parse_time(std::wstring_view text) {
    // Up to three parts separated by ':'. Each part is digits; the last may have ".digits".
    double total = 0;
    int parts = 0;
    std::size_t i = 0;
    for (;;) {
        double value = 0;
        std::size_t digits = 0;
        while (i < text.size() && std::iswdigit(text[i])) {
            value = value * 10 + (text[i] - L'0');
            ++i;
            ++digits;
        }
        bool fraction = false;
        if (i < text.size() && text[i] == L'.') {
            ++i;
            double scale = 0.1;
            while (i < text.size() && std::iswdigit(text[i])) {
                value += (text[i] - L'0') * scale;
                scale /= 10;
                ++i;
                ++digits;
            }
            fraction = true;
        }
        if (digits == 0) {
            return std::nullopt;  // empty part: "", ":30", "1::2", "."
        }
        ++parts;
        // Minutes and seconds after the first part must be below 60 ("1:75" is a typo).
        if (parts > 1 && value >= 60) {
            return std::nullopt;
        }
        total = total * 60 + value;

        if (i == text.size()) {
            return total;
        }
        if (text[i] != L':' || fraction || parts == 3) {
            return std::nullopt;  // junk, a fraction before ':', or too many parts
        }
        ++i;
    }
}

}  // namespace wallflow
