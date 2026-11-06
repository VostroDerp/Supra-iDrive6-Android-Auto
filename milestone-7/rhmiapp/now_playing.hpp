// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_RHMIAPP_NOW_PLAYING_HPP
#define NBT_RHMIAPP_NOW_PLAYING_HPP

// Now playing in iDrive and the instrument cluster (upstream #51), the iDrive app side. The daemon publishes what the
// phone plays as a text record (/dev/shmem/nbt-aad-media, format in src/media_info.hpp) plus the album art as the
// phone sent it (/dev/shmem/nbt-aad-art). The app reads the record once a second and fills the RHMI elements of
// NbtAad_Media.xml:
//   - an <audioHmiState> ("Now playing" in our menu): title, artist, album, cover, progress, times
//   - the <instrumentCluster> list, use case "EntICPlaylist", the way the Connected Apps (and AAIdrive) feed it:
//     "< Previous", the current song (checked), "Next >"; selecting a row asks the daemon for "media prev|play|next"
//   - a <multimediaInfoEvent> (artist, title) and a <statusbarEvent> (the phone's app) on each new song
// Everything here is pure logic, host-tested in tests/test_now_playing.cpp (round trip with the daemon's writer).
// The framework calls are in nbt_aad_app.cpp. Not car-verified.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace nbt {
namespace nowplaying {

const char* const kRecordFile = "/dev/shmem/nbt-aad-media";
const char* const kFlag       = "/var/opt/aa/now-playing";   // on/off, plus options (parseOptions)
const char* const kFlagManual = "/opt/aa/now-playing";
const size_t kRecordMax = 4096;                    // the daemon writes 2048
const size_t kArtMax = 1024 * 1024;                // bigger art is not shown
const char* const kPrevRow = "< Previous";         // AAIdrive's labels for the synthetic rows
const char* const kNextRow = "Next >";
const char* const kUseCase = "EntICPlaylist";

struct Track {
    unsigned seq;
    std::string state;                             // playing | paused | stopped | none
    std::string app, song, artist, album, playlist;
    unsigned duration, position;                   // seconds
    unsigned artSeq, artBytes, artW, artH;
    std::string artType, artPath;                  // png | jpeg | other | none, path or "-"
    Track() : seq(0), state("none"), duration(0), position(0), artSeq(0), artBytes(0), artW(0), artH(0),
              artType("none"), artPath("-") {}
};

// One whole record (first line `nbt-aad-media 1 seq N`, last line `end N`), else false: a reader that catches the
// daemon mid-write sees seq != end and keeps what it showed. Unknown keys are skipped (newer daemon).
inline bool parseRecord(const char* buf, size_t n, Track& out) {
    if (!buf || n == 0) return false;
    std::string s(buf, n);
    static const char kHead[] = "nbt-aad-media 1 seq ";
    if (s.compare(0, sizeof(kHead) - 1, kHead) != 0) return false;
    Track t;
    bool haveEnd = false;
    unsigned endSeq = 0;
    size_t pos = 0;
    bool first = true;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        const std::string line = s.substr(pos, nl - pos);
        pos = nl + 1;
        if (first) {
            first = false;
            t.seq = (unsigned)strtoul(line.c_str() + sizeof(kHead) - 1, NULL, 10);
            continue;
        }
        const size_t sp = line.find(' ');
        const std::string key = line.substr(0, sp);
        const std::string val = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        if (key == "state") t.state = val;
        else if (key == "app") t.app = val;
        else if (key == "song") t.song = val;
        else if (key == "artist") t.artist = val;
        else if (key == "album") t.album = val;
        else if (key == "playlist") t.playlist = val;
        else if (key == "duration") t.duration = (unsigned)strtoul(val.c_str(), NULL, 10);
        else if (key == "position") t.position = (unsigned)strtoul(val.c_str(), NULL, 10);
        else if (key == "art") {
            char type[16] = "", path[256] = "";
            unsigned a = 0, b = 0, w = 0, h = 0;
            if (sscanf(val.c_str(), "%u %15s %u %ux%u %255s", &a, type, &b, &w, &h, path) == 6) {
                t.artSeq = a; t.artType = type; t.artBytes = b; t.artW = w; t.artH = h; t.artPath = path;
            }
        } else if (key == "end") {
            endSeq = (unsigned)strtoul(val.c_str(), NULL, 10);
            haveEnd = true;
            break;
        }
    }
    if (!haveEnd || endSeq != t.seq) return false;
    out = t;
    return true;
}

// What changed between what is on screen and the new record (all of it the first time).
enum { kText = 1, kArt = 2, kProgress = 4, kState = 8 };
inline unsigned changes(const Track& shown, bool haveShown, const Track& now) {
    if (!haveShown) return kText | kArt | kProgress | kState;
    unsigned c = 0;
    if (shown.song != now.song || shown.artist != now.artist || shown.album != now.album || shown.app != now.app)
        c |= kText;
    if (shown.artSeq != now.artSeq || shown.artBytes != now.artBytes) c |= kArt;
    if (shown.position != now.position || shown.duration != now.duration) c |= kProgress;
    if (shown.state != now.state) c |= kState;
    return c;
}

// The art file holds what the record describes: same size and, for PNG / JPEG, the right signature. The daemon writes
// the art before the record, so a mismatch means the next art is being written: try again on the next poll.
inline bool artMatches(const uint8_t* p, size_t n, const Track& t) {
    if (!p || n == 0 || n != t.artBytes || n > kArtMax) return false;
    if (t.artType == "png") return n >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G';
    if (t.artType == "jpeg") return n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF;
    return false;                                  // "other": iDrive would not decode it either
}

// A song is on: show it. "none" (no session) or no title: clear the screens.
inline bool hasSong(const Track& t) { return t.state != "none" && !t.song.empty(); }

// The cluster list: AAIdrive's 7 columns (index, title, artist, album, -1, checked, true), here with the synthetic
// rows only (Android Auto sends no queue).
struct ClusterRow {
    int index;
    std::string title, artist, album;
    bool checked;
};
inline std::vector<ClusterRow> clusterRows(const Track& t) {
    std::vector<ClusterRow> rows;
    if (!hasSong(t)) return rows;
    ClusterRow r;
    r.index = 0; r.title = kPrevRow; r.checked = false; rows.push_back(r);
    r.index = 1; r.title = t.song; r.artist = t.artist; r.album = t.album; r.checked = true; rows.push_back(r);
    r.index = 2; r.title = kNextRow; r.artist.clear(); r.album.clear(); r.checked = false; rows.push_back(r);
    return rows;
}

// A row selected in the cluster list -> the daemon command ("media <cmd>"), NULL = none.
inline const char* commandForRow(int row) {
    return row == 0 ? "prev" : (row == 1 ? "play" : (row == 2 ? "next" : NULL));
}

// The Parcel of the setTrack action, as bytes (bmw::Parcel writes big-endian): the row index first. -1 = unknown.
inline int rowFromParcel(const uint8_t* p, size_t n) {
    if (!p || n < 4) return -1;
    const uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    return v <= 1000 ? (int)v : -1;
}

inline std::string formatTime(unsigned s) {
    char b[24];
    if (s >= 3600) snprintf(b, sizeof(b), "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
    else snprintf(b, sizeof(b), "%u:%02u", s / 60, s % 60);
    return b;
}

// 0..100 for the Progress gauge.
inline int progressPercent(const Track& t) {
    if (!t.duration) return 0;
    const unsigned p = t.position >= t.duration ? 100 : (unsigned)((uint64_t)t.position * 100 / t.duration);
    return (int)p;
}

// UTF-8 -> UTF-16 with a terminating 0 (for BasicString<unsigned short>). Bad bytes become U+FFFD.
inline std::vector<unsigned short> utf16(const std::string& s) {
    std::vector<unsigned short> out;
    out.reserve(s.size() + 1);
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = (unsigned char)s[i];
        unsigned cp = 0xFFFD, len = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else len = 0;                              // stray continuation / invalid lead
        bool ok = len > 0 && i + len <= s.size();
        for (unsigned k = 1; ok && k < len; ++k) {
            const unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (cc & 0x3F);
        }
        if (ok && len > 1) {
            const unsigned minCp = len == 2 ? 0x80 : (len == 3 ? 0x800 : 0x10000);
            if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ok = false;   // overlong / surrogate
        }
        if (!ok) { cp = 0xFFFD; len = 1; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back((unsigned short)(0xD800 + (cp >> 10)));
            out.push_back((unsigned short)(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back((unsigned short)cp);
        }
        i += len;
    }
    out.push_back(0);
    return out;
}

// The flag file turns the feature on. Its lines may set the bmw::Variant type codes of list cells that are not known
// yet (only text = 0x22 is, from libAppFramework's Variant::is<BasicString<char>>): "int-type N", "bool-type N"
// (decimal or 0x hex). Until both are set the cluster list is not sent; the other screens do not need them.
struct Options {
    int intType, boolType, stringType;
    Options() : intType(0), boolType(0), stringType(0x22) {}
    bool listTypesKnown() const { return intType > 0 && boolType > 0; }
};
inline Options parseOptions(const char* text) {
    Options o;
    if (!text) return o;
    const char* p = text;
    while (*p) {
        const char* e = strchr(p, '\n');
        const std::string line = e ? std::string(p, e - p) : std::string(p);
        char key[32] = "";
        char val[32] = "";
        if (sscanf(line.c_str(), "%31s %31s", key, val) == 2) {
            const long v = strtol(val, NULL, 0);
            if (v > 0 && v < 0x10000) {
                if (!strcmp(key, "int-type")) o.intType = (int)v;
                else if (!strcmp(key, "bool-type")) o.boolType = (int)v;
                else if (!strcmp(key, "string-type")) o.stringType = (int)v;
            }
        }
        if (!e) break;
        p = e + 1;
    }
    return o;
}

} // namespace nowplaying
} // namespace nbt

#endif // NBT_RHMIAPP_NOW_PLAYING_HPP
