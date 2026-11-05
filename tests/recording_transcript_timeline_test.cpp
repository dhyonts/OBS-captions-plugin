#include "../src/RecordingTranscriptTimeline.h"
#include <cassert>
#include <iostream>

using Timeline = RecordingTranscriptTimeline;
using namespace std::chrono;

static Timeline::TimePoint at(int seconds) {
    return Timeline::TimePoint{} + std::chrono::seconds(seconds);
}

static void expect_interval(const Timeline &timeline, int start, int end,
                            int expected_start, int expected_end) {
    auto first = at(start);
    auto last = at(end);
    assert(timeline.map_interval(first, last));
    assert(first == at(expected_start));
    assert(last == at(expected_end));
}

int main() {
    Timeline timeline(at(0));
    expect_interval(timeline, 1, 4, 1, 4);
    timeline.resume(at(5)); // Duplicate/out-of-order state notifications are harmless.
    timeline.pause(at(10));
    timeline.pause(at(12));
    assert(timeline.map(at(20)) == at(10));
    auto first = at(12), last = at(18);
    assert(!timeline.map_interval(first, last));
    expect_interval(timeline, 8, 15, 8, 10);
    timeline.resume(at(30));
    timeline.resume(at(32));
    expect_interval(timeline, 35, 38, 15, 18);
    expect_interval(timeline, 8, 35, 8, 15);
    expect_interval(timeline, 20, 35, 10, 15);
    timeline.pause(at(40));
    timeline.resume(at(50));
    expect_interval(timeline, 55, 58, 25, 28);
    // Delayed results must use the pause history at their own receipt times.
    expect_interval(timeline, 3, 6, 3, 6);
    expect_interval(timeline, 35, 38, 15, 18);
    first = at(15); last = at(20);
    assert(!timeline.map_interval(first, last));
    first = at(10); last = at(30);
    assert(!timeline.map_interval(first, last));
    expect_interval(timeline, 30, 33, 10, 13);
    expect_interval(timeline, -1, 1, -1, 1);
    Timeline next_recording(at(100));
    expect_interval(next_recording, 101, 104, 101, 104);
    // A caption received before stop still maps correctly while paused at stop.
    timeline.pause(at(60));
    expect_interval(timeline, 58, 65, 28, 30);
    std::cout << "Recording transcript timeline tests passed\n";
}
