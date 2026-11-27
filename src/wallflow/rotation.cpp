#include "wallflow/rotation.hpp"

#include <algorithm>

namespace wallflow {

void Rotation::start_video(double now, bool paused) {
    played_before_ = 0;
    playing_since_.reset();
    if (!paused) {
        playing_since_ = now;
    }
}

void Rotation::set_paused(bool paused, double now) {
    if (paused && playing_since_) {
        played_before_ += std::max(0.0, now - *playing_since_);
        playing_since_.reset();
    } else if (!paused && !playing_since_) {
        playing_since_ = now;
    }
}

double Rotation::played(double now) const {
    return played_before_ + (playing_since_ ? std::max(0.0, now - *playing_since_) : 0.0);
}

bool Rotation::switch_at_loop_end(double now, double remaining) const {
    return interval_ && played(now) + std::max(0.0, remaining) >= *interval_;
}

double Rotation::preload_delay(double remaining) {
    return std::max(0.0, remaining - kPreloadLead);
}

bool Rotation::keep_waiting_loaded(double loop_length) {
    // Unloading only pays off if the waiting player stays empty for a while: require at least
    // as long empty as it takes to get ready.
    return loop_length <= 2 * kPreloadLead;
}

}  // namespace wallflow
