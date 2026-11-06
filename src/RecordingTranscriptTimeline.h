#ifndef OBS_CAPTIONS_RECORDING_TRANSCRIPT_TIMELINE_H
#define OBS_CAPTIONS_RECORDING_TRANSCRIPT_TIMELINE_H

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

// Keep receipt times in steady-clock coordinates, with recording pauses removed.
// Retaining the intervals also handles recognition results delivered out of order.
class RecordingTranscriptTimeline {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    const TimePoint started_at;

    explicit RecordingTranscriptTimeline(TimePoint start = Clock::now()) : started_at(start) {}

    void pause(TimePoint at = Clock::now()) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!paused) {
            pauses.emplace_back(at, (TimePoint::max)());
            paused = true;
        }
    }

    void resume(TimePoint at = Clock::now()) {
        std::lock_guard<std::mutex> lock(mutex);
        if (paused) {
            pauses.back().second = at;
            paused = false;
        }
    }

    TimePoint map(TimePoint at) const {
        std::lock_guard<std::mutex> lock(mutex);
        return map_locked(at);
    }

    bool map_interval(TimePoint &start, TimePoint &end) const {
        std::lock_guard<std::mutex> lock(mutex);
        const auto mapped_start = map_locked(start);
        const auto mapped_end = map_locked(end);
        // No recorded time remains for a caption wholly inside a pause.
        if (mapped_end <= mapped_start) {
            for (const auto &pause : pauses)
                if (start >= pause.first && end <= pause.second)
                    return false;
        }
        start = mapped_start;
        end = mapped_end;
        return true;
    }

private:
    mutable std::mutex mutex;
    bool paused = false;
    std::vector<std::pair<TimePoint, TimePoint>> pauses;

    TimePoint map_locked(TimePoint at) const {
        auto removed = Clock::duration::zero();
        for (const auto &pause : pauses) {
            if (at <= pause.first)
                break;
            removed += (std::min)(at, pause.second) - pause.first;
        }
        return at - removed;
    }
};

#endif
