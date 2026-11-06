"""Run timing tests and the real dequeue/text writer with a minimal queue adapter.

Requires Python 3 and a C++17 compiler (CXX, default c++). No OBS/Qt required.
This is a focused integration check, not a substitute for building the plugin.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
writer = (root / 'src/caption_transcript_writer.h').read_text()


def function(signature):
    start = writer.index(signature)
    opening = writer.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (writer[end] == '{') - (writer[end] == '}')
        end += 1
    return writer[start:end]


harness = r'''
#include "RecordingTranscriptTimeline.h"
#include <cassert>
#include <deque>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
using namespace std;
enum CapitalizationType { CAPITALIZATION_NORMAL };
using ResultQueue = deque<tuple<RecordingTranscriptTimeline::TimePoint, RecordingTranscriptTimeline::TimePoint, string, bool>>;
struct SrtState;
string srt_entry_caption_text(const SrtState &, const ResultQueue &results, int) { return get<2>(results[0]); }
#define NEWLINE_STR "\n"
struct Result {
    RecordingTranscriptTimeline::TimePoint first_received_at, received_at;
};
struct OutputCaptionResult { Result caption_result; string clean_caption_text; };
struct CaptionOutput { shared_ptr<OutputCaptionResult> output_result; bool is_clearance = false; };
struct Queue {
    deque<CaptionOutput> items;
    bool try_dequeue(CaptionOutput &out) {
        if (items.empty()) return false;
        out = items.front(); items.pop_front(); return true;
    }
    void wait_dequeue(CaptionOutput &out) { assert(try_dequeue(out)); }
};
template<class T> struct CaptionOutputControl {
    bool stop = false;
    Queue caption_queue;
    shared_ptr<RecordingTranscriptTimeline> recording_timeline;
};
'''
harness += '\n' + writer[writer.index('struct SrtState {'):writer.index('\nint batch_up_to(')]
harness += '\n'.join(function(sig) for sig in (
    'template<typename T>\nbool dequeue_caption_output',
    'string time_duration_stream(',
    'void fs_write_string(',
    'void write_transcript_caption_simple(',
    'int write_results_batch_srt(',
))
harness += r'''
int main() {
    using Clock = RecordingTranscriptTimeline::Clock;
    auto at = [](int s) { return Clock::time_point{} + chrono::seconds(s); };
    auto timeline = make_shared<RecordingTranscriptTimeline>(at(0));
    timeline->pause(at(10)); timeline->resume(at(30));
    auto original = make_shared<OutputCaptionResult>();
    original->caption_result = {at(35), at(38)};
    original->clean_caption_text = "after pause";
    CaptionOutputControl<int> recording, streaming;
    recording.recording_timeline = timeline;
    recording.caption_queue.items.push_back({original, false});
    streaming.caption_queue.items.push_back({original, false});
    CaptionOutput adjusted, live;
    assert(dequeue_caption_output(recording, adjusted));
    assert(adjusted.output_result != original);
    assert(adjusted.output_result->caption_result.first_received_at == at(15));
    assert(adjusted.output_result->caption_result.received_at == at(18));
    assert(dequeue_caption_output(streaming, live));
    assert(live.output_result == original);
    assert(original->caption_result.first_received_at == at(35));
    fstream output("caption.txt", ios::out | ios::trunc);
    write_transcript_caption_simple(output, "", at(0), *adjusted.output_result, true);
    output.close();
    ifstream input("caption.txt"); string line; getline(input, line);
    assert(line == "00:00:15,000-00:00:18,000 after pause");
    SrtState settings{};
    settings.transcript_started_at = at(0);
    settings.sequence_number = 1;
    ResultQueue results;
    results.emplace_back(adjusted.output_result->caption_result.first_received_at,
                         adjusted.output_result->caption_result.received_at, "after pause", true);
    output.open("caption.srt", ios::out | ios::trunc);
    assert(write_results_batch_srt(settings, output, results, 0) > 0);
    output.close();
    ifstream srt("caption.srt");
    getline(srt, line); assert(line == "1");
    getline(srt, line); assert(line == "00:00:15,000 --> 00:00:18,000");
    auto paused = make_shared<OutputCaptionResult>();
    paused->caption_result = {at(12), at(20)};
    recording.caption_queue.items.push_back({paused, false});
    assert(dequeue_caption_output(recording, adjusted));
    assert(!adjusted.output_result);
    // Stop drains queued results, then ends rather than blocking.
    recording.stop = true;
    recording.caption_queue.items.push_back({original, false});
    assert(dequeue_caption_output(recording, adjusted));
    assert(adjusted.output_result->caption_result.received_at == at(18));
    assert(!dequeue_caption_output(recording, adjusted));
}
'''

compiler = shlex.split(os.environ.get('CXX', 'c++'))
flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread']
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    subprocess.run(compiler + flags + [str(root / 'tests/windows_minmax_macros_test.cpp'),
                   '-o', str(temp / 'windows_macros')], check=True)
    subprocess.run([str(temp / 'windows_macros')], check=True)
    subprocess.run(compiler + flags + [str(root / 'tests/recording_transcript_timeline_test.cpp'),
                   '-o', str(temp / 'timeline')], check=True)
    subprocess.run([str(temp / 'timeline')], check=True, cwd=temp)
    (temp / 'integration.cpp').write_text(harness)
    subprocess.run(compiler + flags + ['-I', str(root / 'src'), str(temp / 'integration.cpp'),
                   '-o', str(temp / 'integration')], check=True)
    subprocess.run([str(temp / 'integration')], check=True, cwd=temp)
print('Recording transcript dequeue, SRT and text timestamp integration checks passed')
