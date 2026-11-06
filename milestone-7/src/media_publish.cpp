// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "media_publish.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {

MediaPublisher::MediaPublisher()
    : enabled_(false), recordPath_("/dev/shmem/nbt-aad-media"), artPath_("/dev/shmem/nbt-aad-art"), dumpDir_(""),
      dumpLeft_(0), seq_(0), fieldsLogged_(false) {
    pthread_mutex_init(&mx_, NULL);
}

void MediaPublisher::configure(bool enabled, const char* recordPath, const char* artPath, const char* dumpDir,
                               unsigned dumpCount) {
    pthread_mutex_lock(&mx_);
    enabled_ = enabled;
    if (recordPath) recordPath_ = recordPath;
    if (artPath) artPath_ = artPath;
    dumpDir_ = dumpDir ? dumpDir : "";
    dumpLeft_ = dumpCount;
    pthread_mutex_unlock(&mx_);
    if (enabled)
        printf("[Media] now playing for the iDrive app: %s (art %s)%s\n", recordPath_.c_str(), artPath_.c_str(),
               dumpCount ? " + first album arts kept in the dump folder" : "");
    else
        printf("[Media] now playing for the iDrive app: off (NBT_MEDIA_INFO=0)\n");
}

// In place: QNX /dev/shmem has no rename(). The record is always kMediaRecordBytes long, so nothing is left over.
bool MediaPublisher::writeAt(const std::string& path, const uint8_t* p, size_t n, bool truncate) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < n) {
        ssize_t w = pwrite(fd, p + off, n - off, (off_t)off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    if (truncate) (void)ftruncate(fd, (off_t)n);
    close(fd);
    return off == n;
}

void MediaPublisher::publishLocked() {
    const std::string r = mediaRecord(np_, ++seq_, artPath_.c_str());
    if (!writeAt(recordPath_, (const uint8_t*)r.data(), r.size(), false)) {
        static int fails = 0;
        if (++fails <= 3) printf("[Media] could not write %s\n", recordPath_.c_str());
    }
}

void MediaPublisher::onMessage(uint16_t msgId, const uint8_t* body, size_t len) {
    if (msgId == 0x8001) onStatus(body, len);
    else if (msgId == 0x8003) onMetadata(body, len);
    else {
        // 0x8002 MEDIA_PLAYBACK_INPUT has no known body: log the first few, they would tell us what it is.
        static int seen = 0;
        if (++seen <= 3) {
            char hex[3 * 24 + 1]; size_t k = 0;
            for (size_t i = 0; i < len && i < 24; ++i) k += snprintf(hex + k, sizeof(hex) - k, "%02x ", body[i]);
            hex[k] = 0;
            printf("[Media] ch 12 message 0x%04x, %u bytes: %s\n", msgId, (unsigned)len, hex);
        }
    }
}

void MediaPublisher::onStatus(const uint8_t* body, size_t len) {
    AaMediaStatus st;                                // proto3: a missing field is its default, not "unchanged"
    if (!aaParseMediaStatus(body, len, st)) return;
    pthread_mutex_lock(&mx_);
    if (!enabled_) { pthread_mutex_unlock(&mx_); return; }
    const bool changed = st.state != np_.status.state || st.source != np_.status.source;
    np_.status = st;
    publishLocked();
    const std::string line = changed ? aaMediaNowPlayingLine(np_.status, np_.meta) : std::string();
    pthread_mutex_unlock(&mx_);
    if (changed) printf("[Media] %s\n", line.c_str());
}

void MediaPublisher::onMetadata(const uint8_t* body, size_t len) {
    AaMediaMetadata md;
    if (!aaParseMediaMetadata(body, len, md)) return;
    pthread_mutex_lock(&mx_);
    if (!enabled_) { pthread_mutex_unlock(&mx_); return; }
    if (!fieldsLogged_) {                            // once per daemon: which fields this phone fills, and how big
        fieldsLogged_ = true;
        std::string f;
        size_t pos = 0; PbField pf;
        while (pbNextField(body, len, pos, pf)) {
            char b[40];
            snprintf(b, sizeof(b), " %u:%s%u", pf.number, pf.wire == 2 ? "" : "v", pf.wire == 2 ? (unsigned)pf.len : (unsigned)pf.varint);
            f += b;
        }
        printf("[Media] first metadata message: %u bytes, fields (number:bytes, v = varint value):%s\n", (unsigned)len,
               f.c_str());
    }
    const uint8_t* art = md.albumArt;
    const size_t artLen = md.albumArtBytes;
    md.albumArt = NULL;
    np_.meta = md;
    bool newArt = false;
    if (art && artLen) {
        if (writeAt(artPath_, art, artLen, true)) {
            ++np_.artSeq;
            np_.art = mediaSniffArt(art, artLen);
            np_.artBytes = artLen;
            newArt = true;
        }
    } else {
        np_.art = MediaArtInfo();
        np_.artBytes = 0;
    }
    std::string dumped;
    if (newArt && dumpLeft_ && !dumpDir_.empty()) {
        --dumpLeft_;
        char path[256];
        snprintf(path, sizeof(path), "%s/aa-art-%u.%s", dumpDir_.c_str(), np_.artSeq,
                 np_.art.type == MEDIA_ART_PNG ? "png" : (np_.art.type == MEDIA_ART_JPEG ? "jpg" : "bin"));
        if (writeAt(path, art, artLen, true)) dumped = path;
    }
    publishLocked();
    const std::string line = aaMediaNowPlayingLine(np_.status, np_.meta);
    const MediaArtInfo a = np_.art;
    const unsigned artSeq = np_.artSeq;
    pthread_mutex_unlock(&mx_);
    printf("[Media] track: %s\n", line.c_str());
    if (newArt)
        printf("[Media] album art %u: %s %ux%u, %u bytes%s%s\n", artSeq, mediaArtTypeName(a.type), a.width, a.height,
               (unsigned)artLen, dumped.empty() ? "" : ", kept as ", dumped.c_str());
}

void MediaPublisher::onSessionEnded() {
    pthread_mutex_lock(&mx_);
    if (enabled_) {
        const unsigned artSeq = np_.artSeq;          // keep counting: a reader must see the next art as new
        np_ = MediaNowPlaying();
        np_.artSeq = artSeq;
        publishLocked();                             // state none: the iDrive app clears its screens
    }
    pthread_mutex_unlock(&mx_);
}

} // namespace nbt
