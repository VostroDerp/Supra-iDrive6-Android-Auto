// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_MEDIA_PUBLISH_HPP
#define NBT_MEDIA_PUBLISH_HPP

// Publishes what the phone is playing for the iDrive app (record format and media keys: media_info.hpp).
// onMessage / onSessionEnded run on the session thread; all state is behind one mutex. Host-tested with temp paths in
// tests/test_media_info.cpp.

#include "media_info.hpp"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace nbt {

class MediaPublisher {
public:
    MediaPublisher();

    // enabled = NBT_MEDIA_INFO (default on). dumpDir / dumpCount: also keep the first dumpCount album arts as files
    // there (NBT_MEDIA_DUMP=N, 0 = none), to see what the phone sends.
    void configure(bool enabled, const char* recordPath, const char* artPath, const char* dumpDir, unsigned dumpCount);

    // A media playback channel message (ch 12, msg id >= 0x8000) without its 2-byte id.
    void onMessage(uint16_t msgId, const uint8_t* body, size_t len);
    // The session ended: publish "state none" so the iDrive app clears its screens.
    void onSessionEnded();

private:
    MediaPublisher(const MediaPublisher&);
    MediaPublisher& operator=(const MediaPublisher&);
    void onStatus(const uint8_t* body, size_t len);
    void onMetadata(const uint8_t* body, size_t len);
    void publishLocked();
    static bool writeAt(const std::string& path, const uint8_t* p, size_t n, bool truncate);

    pthread_mutex_t mx_;
    bool enabled_;
    std::string recordPath_, artPath_, dumpDir_;
    unsigned dumpLeft_;
    unsigned seq_;
    bool fieldsLogged_;
    MediaNowPlaying np_;
};

} // namespace nbt

#endif // NBT_MEDIA_PUBLISH_HPP
