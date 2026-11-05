"""Test PCM batching, WAV encoding and the actual curl multipart request builder.

The loopback fixture is not a speech model and does not test Qt/OBS integration.
"""
import email.policy
from email.parser import BytesParser
import http.server
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import threading
import wave
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
harness = r'''
#include "LocalWhisperAudio.h"
#include <cassert>
#include <fstream>
using namespace std;
int main(int argc, char **argv) {
    assert(argc == 3);
    assert(whisper_language("en-US") == "en");
    assert(whisper_language("cmn-Hans-CN") == "zh");
    assert(whisper_language("fil-PH") == "tl");
    auto at = [](int milliseconds) { return chrono::steady_clock::time_point{} + chrono::milliseconds(milliseconds); };
    string speech;
    for (int i = 0; i < 16000; ++i) { speech.push_back('\x00'); speech.push_back('\x10'); }
    LocalWhisperAudioBuffer buffer(2);
    assert(buffer.push({speech, at(1000)}).empty());
    const auto segments = buffer.push({speech, at(2000)});
    assert(segments.size() == 1);
    assert(segments[0].start == at(0) && segments[0].end == at(2000));
    assert(segments[0].pcm.size() == 64000);
    const auto wav = whisper_wav(segments[0].pcm);
    assert(wav.substr(0, 4) == "RIFF" && wav.substr(8, 8) == "WAVEfmt ");
    ofstream file(argv[1], ios::binary); file.write(wav.data(), wav.size()); file.close();
    LocalWhisperAudioBuffer quiet(2);
    assert(quiet.push({string(64000, '\0'), at(2000)}).empty());
    LocalWhisperAudioBuffer phrase(5);
    assert(phrase.push({speech, at(1000)}).empty());
    auto phrase_end = phrase.push({string(19200, '\0'), at(1600)});
    assert(phrase_end.size() == 1 && phrase_end[0].end == at(1600));
    LocalWhisperAudioBuffer gap(2);
    gap.push({speech, at(1000)});
    assert(gap.push({speech, at(3000)}).empty());
    auto after_gap = gap.push({speech, at(4000)});
    assert(after_gap.size() == 1 && after_gap[0].start == at(2000));
    LocalWhisperAudioBuffer invalid(2);
    assert(invalid.push({"odd", at(1)}).empty());
    auto args = whisper_curl_arguments(argv[1], argv[2], "en");
    ofstream arguments("arguments.bin", ios::binary);
    for (const auto &argument : args) { arguments.write(argument.data(), argument.size()); arguments.put('\0'); }
}
'''
received = []


class WhisperFixture(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        message = BytesParser(policy=email.policy.default).parsebytes(
            ('Content-Type: ' + self.headers['Content-Type'] + '\r\nMIME-Version: 1.0\r\n\r\n').encode() + body)
        parts = {part.get_param('name', header='content-disposition'): part
                 for part in message.iter_parts()}
        assert self.path == '/inference'
        assert parts['response_format'].get_payload(decode=True) == b'json'
        assert parts['language'].get_payload(decode=True) == b'en'
        audio = parts['file'].get_payload(decode=True)
        with wave.open(io.BytesIO(audio)) as wav:
            assert (wav.getnchannels(), wav.getframerate(), wav.getsampwidth(), wav.getnframes()) == (1, 16000, 2, 32000)
        received.append(audio)
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps({'text': 'Local test caption'}).encode())

    def log_message(self, *args):
        pass


assert shutil.which('curl'), 'curl is required by the local provider'
with http.server.HTTPServer(('127.0.0.1', 0), WhisperFixture) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            (temp / 'local.cpp').write_text(harness)
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) +
                           ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src'),
                            str(temp / 'local.cpp'), '-o', str(temp / 'local')], check=True)
            endpoint = f'http://127.0.0.1:{server.server_port}/inference'
            subprocess.run([str(temp / 'local'), str(temp / 'test audio.wav'), endpoint], check=True, cwd=temp)
            arguments = (temp / 'arguments.bin').read_bytes().decode().split('\0')[:-1]
            response = subprocess.check_output(['curl'] + arguments, timeout=10)
            assert json.loads(response)['text'] == 'Local test caption'
            assert len(received) == 1
    finally:
        server.shutdown()
        thread.join(timeout=2)
ui = ET.parse(root / 'src/ui/CaptionSettingsWidget.ui').getroot()
assert ui.find(".//widget[@name='transcriptionProviderComboBox']") is not None
assert ui.find(".//widget[@name='localWhisperUrlLineEdit']") is not None
print('Local PCM timing, silence/gap handling, WAV encoding, loopback multipart protocol and UI XML tests passed')
