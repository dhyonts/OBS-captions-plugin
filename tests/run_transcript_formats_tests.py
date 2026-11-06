"""Test format parsing/naming, WebVTT rules and production multi-output dispatch."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
source = (root / 'src/caption_transcript_writer.h').read_text()
start = source.index('void transcript_writer_loop(shared_ptr')
end = source.index('\n\n\nvoid fileoutput_writer_loop_inner', start)
dispatch = source[start:end]
harness = r'''
#include "TranscriptFormats.h"
#include "RecordingTranscriptTimeline.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
using namespace std;
struct TranscriptOutputSettings { string format; };
struct CaptionOutput { int value = 0; };
struct Queue {
    mutex lock; condition_variable ready; deque<CaptionOutput> items;
    void enqueue(CaptionOutput value) {
        { lock_guard<mutex> guard(lock); items.push_back(value); }
        ready.notify_one();
    }
    bool try_dequeue(CaptionOutput &out) {
        lock_guard<mutex> guard(lock);
        if (items.empty()) return false;
        out = items.front(); items.pop_front(); return true;
    }
    void wait_dequeue(CaptionOutput &out) {
        unique_lock<mutex> guard(lock);
        ready.wait(guard, [this]{ return !items.empty(); });
        out = items.front(); items.pop_front();
    }
};
template<class T> struct CaptionOutputControl {
    Queue caption_queue; atomic<bool> stop{false}; T arg;
    shared_ptr<RecordingTranscriptTimeline> recording_timeline;
    chrono::system_clock::time_point started_at_sys;
    chrono::steady_clock::time_point started_at_steady;
    CaptionOutputControl(T settings, shared_ptr<RecordingTranscriptTimeline> timeline = nullptr)
        : arg(settings), recording_timeline(timeline) {}
    void stop_soon() { stop = true; caption_queue.enqueue({}); }
};
template<class... T> void error_log(T...) {}
mutex result_lock;
map<string, vector<int>> results;
void transcript_writer_loop_inner(shared_ptr<CaptionOutputControl<TranscriptOutputSettings>> control,
                                  string, TranscriptOutputSettings settings) {
    CaptionOutput output;
    while (true) {
        if (control->stop) {
            if (!control->caption_queue.try_dequeue(output)) break;
        } else control->caption_queue.wait_dequeue(output);
        if (output.value) {
            lock_guard<mutex> guard(result_lock);
            results[settings.format].push_back(output.value);
        }
    }
}
'''
harness += dispatch
harness += r'''
int main() {
    assert(transcript_formats("srt") == vector<string>{"srt"});
    assert((transcript_formats("vtt,srt,vtt,bogus") == vector<string>{"vtt", "srt"}));
    assert(transcript_formats("") == vector<string>{"srt"});
    assert(transcript_formats("bogus") == vector<string>{"srt"});
    const auto formats = transcript_formats("srt,vtt,txt,txt_plain,raw");
    assert(transcript_custom_filename("captions.srt", "vtt") == "captions.vtt");
    assert(transcript_custom_filename("nested/a.txt", "srt") == "nested/a.srt");
    assert(transcript_custom_filename("captions", "srt") == "captions.srt");
    assert(transcript_custom_filename("", "vtt").empty());
    assert(transcript_output_extension("txt_plain", formats) == "plain.txt");
    assert(transcript_output_extension("txt_plain", {"txt_plain"}) == "txt");
    assert(transcript_output_extension("vtt", formats) == "vtt");
    assert(transcript_output_extension("raw", formats) == "log");
    assert(webvtt_timestamp_ms("01:02:03.004") == 3723004);
    assert(webvtt_timestamp_ms("00:03.004") == 3004);
    assert(webvtt_timestamp_ms("01:60:03.004") == -1);
    assert(webvtt_timestamp_ms("00:00:03,004") == -1);
    assert(webvtt_timestamp_ms("999999999999:00:00.000") == -1);
    int offset;
    istringstream invalid("not VTT\n"); assert(!webvtt_append_offset(invalid, offset));
    istringstream valid("WEBVTT\r\n\r\n00:00:01.000 --> 00:00:03.000\r\nHi\r\n");
    assert(webvtt_append_offset(valid, offset) && offset == 3000);
    istringstream zero("WEBVTT\n\n00:00:01.000 --> 00:00:01.000\nHi\n");
    assert(!webvtt_append_offset(zero, offset));
    istringstream bad_order("WEBVTT\n\n00:00:02.000 --> 00:00:03.000\nHi\n\n00:00:01.000 --> 00:00:04.000\nHi\n");
    assert(!webvtt_append_offset(bad_order, offset));
    assert(webvtt_escape_text("<x> & café -->") == "&lt;x&gt; &amp; café --&gt;");
    for (string selection : {"srt", "vtt", "srt,vtt", "srt,vtt,txt,txt_plain,raw"}) {
        results.clear();
        auto control = make_shared<CaptionOutputControl<TranscriptOutputSettings>>(TranscriptOutputSettings{selection});
        // Prequeue then stop: dispatch must drain everything to every selected writer.
        for (int value = 1; value <= 100; ++value) control->caption_queue.enqueue({value});
        control->stop = true;
        transcript_writer_loop(control, "recording", {selection});
        for (const auto &format : transcript_formats(selection)) {
            assert(results[format].size() == 100);
            for (int i = 0; i < 100; ++i) assert(results[format][i] == i + 1);
        }
        assert(results.size() == transcript_formats(selection).size());
    }
}
'''
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp / 'formats.cpp').write_text(harness)
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) +
                   ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(root / 'src'),
                    str(temp / 'formats.cpp'), '-o', str(temp / 'formats')], check=True)
    subprocess.run([str(temp / 'formats')], check=True, timeout=20)
ui = ET.parse(root / 'src/ui/CaptionSettingsWidget.ui').getroot()
button = ui.find(".//widget[@name='transcriptFormatButton']")
assert button is not None and button.attrib['class'] == 'QToolButton'
assert button.find("property[@name='popupMode']/enum").text == 'QToolButton::InstantPopup'
widget_source = (root / 'src/ui/CaptionSettingsWidget.cpp').read_text()
assert widget_source.index('this->updateUi();') > widget_source.index('transcriptFormatButton->setMenu(')
print('Format selection, filenames, WebVTT validation, UI XML, and multi-output drain tests passed')
