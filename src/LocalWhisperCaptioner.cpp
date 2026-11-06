#include "LocalWhisperCaptioner.h"
#include "log.h"
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <stdexcept>

LocalWhisperCaptioner::LocalWhisperCaptioner(
        const std::string &url, const std::string &spoken_language, int chunk_seconds,
        std::function<void(const CaptionResult &, bool)> on_caption)
        : audio(chunk_seconds), callback(std::move(on_caption)) {
    QUrl address(QString::fromStdString(url));
    const auto host = address.host().toLower();
    if (!address.isValid() || address.scheme() != "http" ||
        (host != "127.0.0.1" && host != "::1" && host != "localhost") ||
        !address.userInfo().isEmpty() || address.hasQuery() || address.hasFragment() ||
        address.port(80) < 1 || address.port(80) > 65535)
        throw std::runtime_error("Local Whisper requires an http:// loopback endpoint");
    if (host == "localhost") address.setHost("127.0.0.1");
    if (address.path().isEmpty() || address.path() == "/") address.setPath("/inference");
    endpoint = address.toString(QUrl::FullyEncoded);
    curl = QStandardPaths::findExecutable("curl");
    if (curl.isEmpty()) throw std::runtime_error("Local Whisper requires curl on PATH");
    language = QString::fromStdString(whisper_language(spoken_language)).toLower();
}

void LocalWhisperCaptioner::queue_audio_data(const CapturedAudioChunk &chunk) {
    for (const auto &segment : audio.push(chunk)) {
        if (stopping) return;
        transcribe(segment);
    }
}

void LocalWhisperCaptioner::transcribe(const LocalWhisperSegment &segment) {
    QTemporaryFile file(QDir::tempPath() + "/obs-local-whisper-XXXXXX.wav");
    const auto wav = whisper_wav(segment.pcm);
    if (!file.open() || file.write(wav.data(), static_cast<qint64>(wav.size())) != static_cast<qint64>(wav.size()) || !file.flush()) {
        warn_log("Local Whisper: could not create temporary WAV");
        return;
    }
    const auto filename = file.fileName();
    file.close(); // Allow curl to read it on Windows as well.
    QProcess process;
    // No shell, redirects, proxies, credentials or user curl config. The endpoint
    // is always loopback and failures never fall back to a cloud service.
    QStringList args;
    for (const auto &argument : whisper_curl_arguments(filename.toStdString(), endpoint.toStdString(), language.toStdString()))
        args << QString::fromStdString(argument);
    process.start(curl, args, QIODevice::ReadOnly);
    if (!process.waitForStarted(2000)) {
        warn_log("Local Whisper: could not start curl");
        return;
    }
    QByteArray response, errors;
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(50)) {
        response += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if (stopping || timer.elapsed() > 35000 || response.size() > 1024 * 1024 || errors.size() > 65536) {
            process.kill(); process.waitForFinished(1000);
            if (!stopping) warn_log("Local Whisper: request exceeded time or response size limit");
            return;
        }
        if (process.state() == QProcess::NotRunning) break;
    }
    response += process.readAllStandardOutput();
    if (stopping) return;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || response.size() > 1024 * 1024) {
        warn_log("Local Whisper request failed; check the local server and OBS settings (curl exit %d)", process.exitCode());
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("text").isString()) {
        warn_log("Local Whisper: expected a JSON object with a text field");
        return;
    }
    const auto text = document.object().value("text").toString().trimmed().toStdString();
    if (text.empty()) return;
    callback(CaptionResult(index++, true, 1.0, text, "", segment.start, segment.end), false);
}
