// Windows headers can define function-like macros even when these C++ standard
// headers have already loaded. Public plugin headers must compile in that order.
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define min(a, b) unexpected_windows_min_macro(a, b)
#define max(a, b) unexpected_windows_max_macro(a, b)

#include "../src/RecordingTranscriptTimeline.h"
#if __has_include("../src/TranscriptFormats.h")
#include "../src/TranscriptFormats.h"
#endif
#if __has_include("../src/LocalWhisperAudio.h")
#include "../src/LocalWhisperAudio.h"
#endif

#if !defined(min) || !defined(max)
#error Plugin headers must not remove the caller's Windows macros
#endif

int main() {
    using Clock = RecordingTranscriptTimeline::Clock;
    const auto start = Clock::time_point{};
    RecordingTranscriptTimeline timeline(start);
    timeline.pause(start + std::chrono::seconds(10));
    timeline.resume(start + std::chrono::seconds(30));
    assert(timeline.map(start + std::chrono::seconds(35)) == start + std::chrono::seconds(15));
#if __has_include("../src/TranscriptFormats.h")
    assert(webvtt_timestamp_ms("01:02:03.004") == 3723004);
    std::istringstream vtt("WEBVTT\n\n00:00:01.000 --> 00:00:03.000\nHello\n");
    int offset = 0;
    assert(webvtt_append_offset(vtt, offset) && offset == 3000);
#endif
}
