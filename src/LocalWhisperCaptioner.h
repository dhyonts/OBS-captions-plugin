#ifndef OBS_CAPTIONS_LOCAL_WHISPER_CAPTIONER_H
#define OBS_CAPTIONS_LOCAL_WHISPER_CAPTIONER_H

#include "LocalWhisperAudio.h"
#include <atomic>
#include <functional>
#include <CaptionResult.h>
#include <QString>

// A whisper.cpp server owns the model; OBS only sends bounded WAV requests.
class LocalWhisperCaptioner {
    QString curl, endpoint, language;
    LocalWhisperAudioBuffer audio;
    std::function<void(const CaptionResult &, bool)> callback;
    std::atomic<bool> stopping{false};
    int index = 0;
    void transcribe(const LocalWhisperSegment &segment);
public:
    LocalWhisperCaptioner(const std::string &url, const std::string &language, int chunk_seconds,
                         std::function<void(const CaptionResult &, bool)> callback);
    void queue_audio_data(const CapturedAudioChunk &chunk);
    void request_stop() { stopping = true; }
};

#endif
