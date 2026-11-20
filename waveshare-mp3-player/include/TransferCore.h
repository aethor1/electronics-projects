#pragma once
// Host-testable helpers for Wi-Fi upload and USB drive mode (no Arduino types).
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace player {

// Same rule as the Mac app's PlayerName.forSong(): printable ASCII only, FAT-safe,
// stem <= 96 bytes, no leading/trailing spaces or dots, always ".mp3".
// Works per byte so non-ASCII UTF-8 becomes one '_' per byte on both sides.
inline std::string sanitizeUploadName(const std::string &raw) {
    std::string name = raw;
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);  // browsers may send paths
    if (name.size() >= 4) {
        const std::string tail = name.substr(name.size() - 4);
        if ((tail[0] == '.') && (tail[1] == 'm' || tail[1] == 'M') && (tail[2] == 'p' || tail[2] == 'P') &&
            tail[3] == '3') name.resize(name.size() - 4);
    }
    for (char &c : name) {
        const unsigned char b = static_cast<unsigned char>(c);
        const bool forbidden = b == '\\' || b == '/' || b == ':' || b == '*' || b == '?' || b == '"' ||
                               b == '<' || b == '>' || b == '|';
        if (b < 0x20 || b > 0x7E || forbidden) c = '_';
    }
    if (name.size() > 96) name.resize(96);
    size_t start = 0;
    while (start < name.size() && (name[start] == ' ' || name[start] == '.')) ++start;
    size_t end = name.size();
    while (end > start && (name[end - 1] == ' ' || name[end - 1] == '.')) --end;
    name = name.substr(start, end - start);
    if (name.empty()) name = "song";
    return name + ".mp3";
}

inline bool hasMp3Extension(const std::string &name) {
    const size_t n = name.size();
    return n >= 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 'm' && (name[n - 2] | 0x20) == 'p' &&
           name[n - 1] == '3';
}

// "Song.mp3" -> "Song (2).mp3" for attempt 2, etc.
inline std::string numberedName(const std::string &mp3Name, unsigned attempt) {
    if (attempt < 2) return mp3Name;
    const std::string stem = mp3Name.substr(0, mp3Name.size() - 4);
    return stem + " (" + std::to_string(attempt) + ").mp3";
}

// Wi-Fi join QR payload (ZXing "WIFI:" format); \ ; , " : must be escaped.
inline std::string wifiQrEscape(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '\\' || c == ';' || c == ',' || c == '"' || c == ':') out += '\\';
        out += c;
    }
    return out;
}
inline std::string wifiQrPayload(const std::string &ssid, const std::string &password) {
    return "WIFI:T:WPA;S:" + wifiQrEscape(ssid) + ";P:" + wifiQrEscape(password) + ";;";
}

// Minimal JSON string escaping for file names in /api/files.
inline std::string jsonEscape(const std::string &text) {
    std::string out;
    for (char c : text) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (b < 0x20) {
            char buf[8];
            static const char hex[] = "0123456789abcdef";
            buf[0] = '\\'; buf[1] = 'u'; buf[2] = '0'; buf[3] = '0';
            buf[4] = hex[b >> 4]; buf[5] = hex[b & 15]; buf[6] = 0;
            out += buf;
        } else out += c;
    }
    return out;
}

// Upload/USB session idle timer; millis() wrap-safe.
inline bool sessionExpired(uint32_t now, uint32_t lastActivity, uint32_t timeoutMs) {
    return uint32_t(now - lastActivity) >= timeoutMs;
}
inline uint32_t sessionSecondsLeft(uint32_t now, uint32_t lastActivity, uint32_t timeoutMs) {
    const uint32_t idle = uint32_t(now - lastActivity);
    return idle >= timeoutMs ? 0 : (timeoutMs - idle + 999) / 1000;
}

}  // namespace player
