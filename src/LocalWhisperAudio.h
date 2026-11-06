#ifndef OBS_CAPTIONS_LOCAL_WHISPER_AUDIO_H
#define OBS_CAPTIONS_LOCAL_WHISPER_AUDIO_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

inline std::string whisper_language(std::string language) {
    language = language.substr(0, language.find_first_of("-_"));
    if (language == "cmn") return "zh";
    if (language == "fil") return "tl";
    if (language == "nb") return "no";
    if (language == "iw") return "he";
    return language.empty() ? "auto" : language;
}

struct CapturedAudioChunk {
    std::string pcm;
    std::chrono::steady_clock::time_point captured_at;
};

struct LocalWhisperSegment {
    std::string pcm;
    std::chrono::steady_clock::time_point start, end;
};

inline std::vector<std::string> whisper_curl_arguments(const std::string &filename,
                                                       const std::string &endpoint,
                                                       const std::string &language) {
    return {"--disable", "--silent", "--show-error", "--fail", "--noproxy", "*",
            "--proto", "=http", "--connect-timeout", "2", "--max-time", "30",
            "--form", "file=@\"" + filename + "\";type=audio/wav",
            "--form-string", "response_format=json", "--form-string", "temperature=0.0",
            "--form-string", "language=" + language, "--url", endpoint};
}

inline std::string whisper_wav(const std::string &pcm) {
    std::string wav;
    auto number = [&wav](uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) wav.push_back(static_cast<char>((value >> (8 * i)) & 255));
    };
    wav += "RIFF"; number(static_cast<uint32_t>(pcm.size()) + 36, 4); wav += "WAVEfmt ";
    number(16, 4); number(1, 2); number(1, 2); number(16000, 4); number(32000, 4);
    number(2, 2); number(16, 2); wav += "data"; number(static_cast<uint32_t>(pcm.size()), 4);
    wav += pcm;
    return wav;
}

// Segment already-resampled 16 kHz mono signed-16 PCM. Endpoints refer to audio
// capture, not inference completion, so slow inference does not shift subtitles.
class LocalWhisperAudioBuffer {
    std::string pending;
    std::chrono::steady_clock::time_point start{}, last_end{};
    size_t silence_bytes = 0;
    bool has_speech = false;
    const size_t max_bytes;
public:
    explicit LocalWhisperAudioBuffer(int seconds)
        : max_bytes(static_cast<size_t>(std::clamp(seconds, 2, 15)) * 32000) {}

    std::vector<LocalWhisperSegment> push(const CapturedAudioChunk &chunk) {
        std::vector<LocalWhisperSegment> segments;
        if (chunk.pcm.empty() || chunk.pcm.size() % 2) return segments;
        const auto duration = std::chrono::microseconds(chunk.pcm.size() * 1000000 / 32000);
        const auto chunk_start = chunk.captured_at - duration;
        if (!pending.empty() && chunk_start - last_end > std::chrono::milliseconds(250)) {
            // The bounded input queue dropped audio: never invent continuous time.
            pending.clear(); silence_bytes = 0; has_speech = false;
        }
        if (pending.empty()) start = chunk_start;
        uint64_t amplitude = 0;
        for (size_t i = 0; i < chunk.pcm.size(); i += 2) {
            const auto bits = static_cast<uint16_t>(static_cast<unsigned char>(chunk.pcm[i])) |
                              (static_cast<uint16_t>(static_cast<unsigned char>(chunk.pcm[i + 1])) << 8);
            const int sample = bits < 32768 ? bits : static_cast<int>(bits) - 65536;
            amplitude += sample < 0 ? -sample : sample;
        }
        const bool speech = amplitude / (chunk.pcm.size() / 2) > 300;
        has_speech |= speech;
        silence_bytes = speech ? 0 : silence_bytes + chunk.pcm.size();
        pending += chunk.pcm;
        last_end = chunk.captured_at;
        if (pending.size() >= max_bytes || (pending.size() >= 32000 && silence_bytes >= 19200)) {
            if (has_speech) segments.push_back({std::move(pending), start, last_end});
            pending.clear(); silence_bytes = 0; has_speech = false;
        }
        return segments;
    }
};

#endif
