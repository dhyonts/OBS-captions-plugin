#ifndef OBS_CAPTIONS_TRANSCRIPT_FORMATS_H
#define OBS_CAPTIONS_TRANSCRIPT_FORMATS_H

#include <algorithm>
#include <sstream>
#include <limits>
#include <regex>
#include <string>
#include <vector>

// Single legacy values remain valid; multiple selections share the same setting.
inline std::vector<std::string> transcript_formats(const std::string &selection) {
    std::vector<std::string> result;
    std::istringstream input(selection);
    std::string format;
    while (std::getline(input, format, ',')) {
        if (format != "srt" && format != "vtt" && format != "txt" &&
            format != "txt_plain" && format != "raw")
            continue;
        if (std::find(result.begin(), result.end(), format) == result.end())
            result.push_back(format);
    }
    if (result.empty())
        result.push_back("srt");
    return result;
}

inline std::string transcript_output_extension(const std::string &format,
                                                const std::vector<std::string> &formats) {
    if (format == "raw") return "log";
    if (format == "txt_plain") {
        // Both text formats must have different names when selected together.
        return std::find(formats.begin(), formats.end(), "txt") != formats.end() ? "plain.txt" : "txt";
    }
    return format;
}

inline std::string transcript_custom_filename(std::string name, const std::string &extension) {
    // A single format still uses the exact custom name. Multi-format output uses
    // that name as a base and gives every file its own extension.
    for (const std::string suffix : {".plain.txt", ".srt", ".vtt", ".txt", ".log"}) {
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name.resize(name.size() - suffix.size());
            break;
        }
    }
    return name.empty() ? std::string() : name + "." + extension;
}

inline std::string webvtt_escape_text(const std::string &text) {
    std::string escaped;
    for (char c : text) {
        if (c == '&') escaped += "&amp;";
        else if (c == '<') escaped += "&lt;";
        else if (c == '>') escaped += "&gt;";
        else escaped += c;
    }
    return escaped;
}

inline bool webvtt_signature(const std::string &line) {
    const auto start = line.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3u : 0u;
    return line.compare(start, 6, "WEBVTT") == 0 &&
           (line.size() == start + 6 || line[start + 6] == '\r' ||
            line[start + 6] == ' ' || line[start + 6] == '\t') &&
           line.find("-->") == std::string::npos;
}

inline int webvtt_timestamp_ms(const std::string &value) {
    const std::regex timestamp(R"((?:(\d{2,}):)?([0-5]\d):([0-5]\d)\.(\d{3}))");
    std::smatch match;
    if (!std::regex_match(value, match, timestamp)) return -1;
    try {
        const auto hours = match[1].matched ? std::stoll(match[1]) : 0;
        if (hours > (std::numeric_limits<int>::max)() / 3600000) return -1;
        const auto milliseconds = hours * 3600000 + std::stoi(match[2]) * 60000 +
                                  std::stoi(match[3]) * 1000 + std::stoi(match[4]);
        return milliseconds <= (std::numeric_limits<int>::max)() ? static_cast<int>(milliseconds) : -1;
    } catch (...) { return -1; }
}

inline bool webvtt_append_offset(std::istream &existing, int &offset) {
    std::string line;
    if (!std::getline(existing, line) || !webvtt_signature(line)) return false;
    offset = 0;
    int previous_start = -1;
    while (std::getline(existing, line)) {
        const auto arrow = line.find(" --> ");
        if (arrow == std::string::npos) continue;
        const auto start = webvtt_timestamp_ms(line.substr(0, arrow));
        const auto end_begin = arrow + 5;
        const auto end_end = line.find_first_of(" \t\r", end_begin);
        const auto end = webvtt_timestamp_ms(line.substr(end_begin, end_end - end_begin));
        if (start < previous_start || start < 0 || end <= start) return false;
        previous_start = start;
        offset = (std::max)(offset, end);
    }
    return !existing.bad();
}

#endif
