// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_MEDIA_INFO_HPP
#define NBT_MEDIA_INFO_HPP

// What the phone is playing, for the iDrive app (now playing in iDrive and the instrument cluster, upstream #51).
// The phone sends it on the media playback channel (ch 12): 0x8001 status about once a second, 0x8003 metadata with
// the album art on each track (audio/aa_media_meta.hpp parses both). The daemon keeps the merged state and publishes
// it as a small text record, /dev/shmem/nbt-aad-media, written in place (QNX /dev/shmem has no rename()):
//
//   nbt-aad-media 1 seq <n>            first line: format version and a sequence number
//   state <playing|paused|stopped|none>
//   app <name>                         the phone's media app ("Spotify", "YouTube Music", ...)
//   song <text>
//   artist <text>
//   album <text>
//   playlist <text>
//   duration <s>
//   position <s>
//   art <seq> <png|jpeg|other|none> <bytes> <w>x<h> <path>
//   end <n>                            last line: the same n as on the first line
//
// A reader that finds `seq n` and `end n` equal has a whole record (the writer pads the rest of the file with
// spaces). Text is one line, at most kMaxText bytes, control characters replaced. The art file
// (/dev/shmem/nbt-aad-art) holds the bytes the phone sent, as they are; `art <seq>` changes when it does.
//
// The iDrive app asks for control with "media next|prev|playpause|play|pause" on the daemon's control socket
// (UDP 127.0.0.1:52790); the daemon sends the phone the matching Android media key (mediaKeyFor()).
// Header-only and side-effect free, host-tested in tests/test_media_info.cpp.

#include "audio/aa_media_meta.hpp"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace nbt {

enum { kMediaMaxText = 200, kMediaRecordBytes = 2048 };

enum MediaArtType { MEDIA_ART_NONE = 0, MEDIA_ART_PNG, MEDIA_ART_JPEG, MEDIA_ART_OTHER };

struct MediaArtInfo {
    MediaArtType type;
    unsigned width, height;     // 0 = not found
    MediaArtInfo() : type(MEDIA_ART_NONE), width(0), height(0) {}
};

inline const char* mediaArtTypeName(MediaArtType t) {
    return t == MEDIA_ART_PNG ? "png" : (t == MEDIA_ART_JPEG ? "jpeg" : (t == MEDIA_ART_OTHER ? "other" : "none"));
}

// PNG: the IHDR chunk right after the signature. JPEG: the first SOFn marker.
inline MediaArtInfo mediaSniffArt(const uint8_t* p, size_t n) {
    MediaArtInfo a;
    if (!p || n == 0) return a;
    static const uint8_t kPng[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n >= 8 && memcmp(p, kPng, 8) == 0) {
        a.type = MEDIA_ART_PNG;
        if (n >= 24 && memcmp(p + 12, "IHDR", 4) == 0) {
            a.width = ((unsigned)p[16] << 24) | ((unsigned)p[17] << 16) | ((unsigned)p[18] << 8) | p[19];
            a.height = ((unsigned)p[20] << 24) | ((unsigned)p[21] << 16) | ((unsigned)p[22] << 8) | p[23];
        }
        return a;
    }
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) {
        a.type = MEDIA_ART_JPEG;
        size_t i = 2;
        while (i + 4 <= n) {
            if (p[i] != 0xFF) { ++i; continue; }
            const uint8_t m = p[i + 1];
            if (m == 0xFF) { ++i; continue; }                       // fill byte
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }   // no length
            const size_t len = ((size_t)p[i + 2] << 8) | p[i + 3];
            const bool sof = m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC;
            if (sof && i + 9 <= n) {
                a.height = ((unsigned)p[i + 5] << 8) | p[i + 6];
                a.width = ((unsigned)p[i + 7] << 8) | p[i + 8];
                break;
            }
            if (m == 0xDA || len < 2) break;                         // scan data: no size before it
            i += 2 + len;
        }
        return a;
    }
    a.type = MEDIA_ART_OTHER;
    return a;
}

// One line, at most kMediaMaxText bytes, control characters (incl. newlines) as spaces; a cut never splits a UTF-8
// sequence.
inline std::string mediaCleanText(const std::string& in) {
    std::string s(in);
    for (size_t i = 0; i < s.size(); ++i)
        if ((unsigned char)s[i] < 0x20 || s[i] == 0x7F) s[i] = ' ';
    if (s.size() > (size_t)kMediaMaxText) {
        size_t cut = kMediaMaxText;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;   // back to the start of a character
        s.resize(cut);
    }
    return s;
}

// The merged state the record is made from.
struct MediaNowPlaying {
    AaMediaStatus status;
    AaMediaMetadata meta;       // meta.albumArt is not used here (it points into a session buffer)
    unsigned artSeq;            // changes with each new album art
    MediaArtInfo art;
    size_t artBytes;
    MediaNowPlaying() : artSeq(0), artBytes(0) {}
};

inline const char* mediaStateWord(unsigned s) {
    return s == 2 ? "playing" : (s == 3 ? "paused" : (s == 1 ? "stopped" : "none"));
}

// The record, padded with spaces to kMediaRecordBytes (with a final newline) so an in-place rewrite always replaces
// the whole file. artPath = "" when there is no art file.
inline std::string mediaRecord(const MediaNowPlaying& np, unsigned seq, const char* artPath) {
    char head[48];
    snprintf(head, sizeof(head), "nbt-aad-media 1 seq %u\n", seq);
    std::string r(head);
    r += "state "; r += mediaStateWord(np.status.state); r += "\n";
    r += "app "; r += mediaCleanText(np.status.source); r += "\n";
    r += "song "; r += mediaCleanText(np.meta.song); r += "\n";
    r += "artist "; r += mediaCleanText(np.meta.artist); r += "\n";
    r += "album "; r += mediaCleanText(np.meta.album); r += "\n";
    r += "playlist "; r += mediaCleanText(np.meta.playlist); r += "\n";
    char nums[160];
    snprintf(nums, sizeof(nums), "duration %u\nposition %u\nart %u %s %u %ux%u ", np.meta.duration,
             np.status.position, np.artSeq, mediaArtTypeName(np.art.type), (unsigned)np.artBytes, np.art.width,
             np.art.height);
    r += nums;
    r += (artPath && *artPath && np.artBytes) ? artPath : "-";
    char tail[32];
    snprintf(tail, sizeof(tail), "\nend %u\n", seq);
    r += tail;
    if (r.size() < (size_t)kMediaRecordBytes) {
        r.insert(r.size() - 1, (size_t)kMediaRecordBytes - r.size(), ' ');   // pad before the final newline
    }
    return r;
}

// "media <cmd>" from the iDrive app -> Android keycode (0 = unknown). The input channel advertises all five.
inline uint32_t mediaKeyFor(const char* cmd) {
    if (!cmd) return 0;
    if (!strncmp(cmd, "next", 4)) return 87;        // KEYCODE_MEDIA_NEXT
    if (!strncmp(cmd, "prev", 4)) return 88;        // KEYCODE_MEDIA_PREVIOUS
    if (!strncmp(cmd, "playpause", 9)) return 85;   // KEYCODE_MEDIA_PLAY_PAUSE
    if (!strncmp(cmd, "play", 4)) return 126;       // KEYCODE_MEDIA_PLAY
    if (!strncmp(cmd, "pause", 5)) return 127;      // KEYCODE_MEDIA_PAUSE
    return 0;
}

} // namespace nbt

#endif // NBT_MEDIA_INFO_HPP
