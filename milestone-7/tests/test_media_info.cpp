// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: now playing for the iDrive app (media_info.hpp, media_publish.cpp). The record format the iDrive app
// reads, text cleaning, album-art sniffing (PNG / JPEG sizes), the media keys, and the publisher end to end with
// temp files: status + metadata merge, the art file, session end.
#include "media_info.hpp"
#include "media_publish.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void varint(std::vector<uint8_t>& b, uint64_t v) {
    while (v >= 0x80) { b.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    b.push_back((uint8_t)v);
}
static void fieldVarint(std::vector<uint8_t>& b, unsigned n, uint64_t v) { varint(b, (n << 3) | 0); varint(b, v); }
static void fieldBytes(std::vector<uint8_t>& b, unsigned n, const void* p, size_t len) {
    varint(b, (n << 3) | 2); varint(b, len);
    b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + len);
}
static void fieldStr(std::vector<uint8_t>& b, unsigned n, const char* s) { fieldBytes(b, n, s, strlen(s)); }

static std::vector<uint8_t> png(unsigned w, unsigned h) {
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> p(sig, sig + 8);
    const uint8_t ihdr[8] = { 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    p.insert(p.end(), ihdr, ihdr + 8);
    const uint8_t dims[8] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                              (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h };
    p.insert(p.end(), dims, dims + 8);
    p.resize(p.size() + 100, 0x42);
    return p;
}

static std::string readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string s; char b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}
static std::string line(const std::string& rec, const char* key) {   // the value after "key "
    const std::string k = std::string("\n") + key + " ";
    size_t i = rec.find(k);
    if (i == std::string::npos) return "<missing>";
    i += k.size();
    return rec.substr(i, rec.find('\n', i) - i);
}

int main() {
    // 1. Album art sniffing.
    {
        std::vector<uint8_t> p = png(300, 240);
        MediaArtInfo a = mediaSniffArt(&p[0], p.size());
        CHECK(a.type == MEDIA_ART_PNG && a.width == 300 && a.height == 240);
        // JPEG: SOI, APP0 (len 16), SOF0 with height 0x01E0 (480), width 0x0280 (640).
        const uint8_t j[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0,
                              0xFF, 0xC0, 0x00, 0x11, 0x08, 0x01, 0xE0, 0x02, 0x80, 0x03, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1 };
        a = mediaSniffArt(j, sizeof(j));
        CHECK(a.type == MEDIA_ART_JPEG && a.width == 640 && a.height == 480);
        const uint8_t junk[] = { 1, 2, 3, 4 };
        CHECK(mediaSniffArt(junk, 4).type == MEDIA_ART_OTHER);
        CHECK(mediaSniffArt(NULL, 0).type == MEDIA_ART_NONE);
        CHECK(mediaSniffArt(&p[0], 10).type == MEDIA_ART_PNG && mediaSniffArt(&p[0], 10).width == 0);   // cut short
    }

    // 2. Text: one line, control characters gone, cut at a character boundary.
    {
        CHECK(mediaCleanText("a\nb\tc") == "a b c");
        std::string longer;
        for (int i = 0; i < 150; ++i) longer += "\xc3\xa9";            // 300 bytes of e-acute
        const std::string c = mediaCleanText(longer);
        CHECK(c.size() <= (size_t)kMediaMaxText && c.size() % 2 == 0);
    }

    // 3. Media keys.
    CHECK(mediaKeyFor("next") == 87 && mediaKeyFor("prev") == 88 && mediaKeyFor("playpause") == 85);
    CHECK(mediaKeyFor("play") == 126 && mediaKeyFor("pause") == 127 && mediaKeyFor("stop") == 0 && mediaKeyFor(NULL) == 0);

    // 4. The record: fixed size, seq == end, every key present.
    {
        MediaNowPlaying np;
        np.status.state = 2; np.status.source = "Spotify"; np.status.position = 42;
        np.meta.song = "Song\nTitle"; np.meta.artist = "Artist"; np.meta.album = "Album"; np.meta.duration = 200;
        const std::string r = mediaRecord(np, 7, "/tmp/art");
        CHECK(r.size() == (size_t)kMediaRecordBytes && r[r.size() - 1] == '\n');
        CHECK(r.compare(0, 22, "nbt-aad-media 1 seq 7\n") == 0);
        CHECK(line(r, "state") == "playing" && line(r, "app") == "Spotify" && line(r, "song") == "Song Title");
        CHECK(line(r, "duration") == "200" && line(r, "position") == "42");
        CHECK(line(r, "art") == "0 none 0 0x0 -");                 // no art: no path
        CHECK(line(r, "end").substr(0, 1) == "7");
    }

    // 5. The publisher end to end, with temp files.
    {
        char dir[] = "/tmp/test_media_info.XXXXXX";
        CHECK(mkdtemp(dir) != NULL);
        const std::string rec = std::string(dir) + "/media", art = std::string(dir) + "/art";
        MediaPublisher pub;
        pub.configure(true, rec.c_str(), art.c_str(), dir, 1);

        std::vector<uint8_t> st;
        fieldVarint(st, 1, 2); fieldStr(st, 2, "YouTube Music"); fieldVarint(st, 3, 5);
        pub.onMessage(0x8001, &st[0], st.size());
        std::string r = readFile(rec);
        CHECK(line(r, "state") == "playing" && line(r, "app") == "YouTube Music" && line(r, "position") == "5");

        const std::vector<uint8_t> img = png(128, 128);
        std::vector<uint8_t> md;
        fieldStr(md, 1, "Lightyears"); fieldStr(md, 2, "Ente"); fieldStr(md, 3, "First"); fieldBytes(md, 4, &img[0], img.size());
        fieldVarint(md, 6, 215);
        pub.onMessage(0x8003, &md[0], md.size());
        r = readFile(rec);
        CHECK(line(r, "song") == "Lightyears" && line(r, "artist") == "Ente" && line(r, "duration") == "215");
        CHECK(line(r, "state") == "playing");                          // metadata keeps the last status
        char want[200];
        snprintf(want, sizeof(want), "1 png %u 128x128 %s", (unsigned)img.size(), art.c_str());
        CHECK(line(r, "art") == want);
        CHECK(readFile(art) == std::string(img.begin(), img.end()));
        CHECK(readFile(std::string(dir) + "/aa-art-1.png") == std::string(img.begin(), img.end()));   // dump 1 of 1

        // A shorter second art truncates the file; no second dump.
        const std::vector<uint8_t> img2 = png(64, 64);
        std::vector<uint8_t> md2;
        fieldStr(md2, 1, "Next one"); fieldBytes(md2, 4, &img2[0], img2.size());
        pub.onMessage(0x8003, &md2[0], md2.size());
        CHECK(readFile(art).size() == img2.size());
        CHECK(readFile(std::string(dir) + "/aa-art-2.png").empty());
        r = readFile(rec);
        CHECK(line(r, "song") == "Next one" && line(r, "artist") == "" && line(r, "art").substr(0, 6) == "2 png ");

        // Pause: status alone.
        std::vector<uint8_t> st2;
        fieldVarint(st2, 1, 3); fieldStr(st2, 2, "YouTube Music");
        pub.onMessage(0x8001, &st2[0], st2.size());
        CHECK(line(readFile(rec), "state") == "paused" && line(readFile(rec), "song") == "Next one");

        // Session end: state none, text gone, the art sequence keeps counting.
        pub.onSessionEnded();
        r = readFile(rec);
        CHECK(line(r, "state") == "none" && line(r, "song") == "" && line(r, "art").substr(0, 2) == "2 ");

        // Off: nothing written.
        MediaPublisher off;
        const std::string rec2 = std::string(dir) + "/media-off";
        off.configure(false, rec2.c_str(), art.c_str(), "", 0);
        off.onMessage(0x8001, &st[0], st.size());
        CHECK(readFile(rec2).empty());

        // Garbage and 0x8002 don't crash or write a bad record.
        const uint8_t bad[] = { 0xFF, 0xFF, 0xFF };
        pub.onMessage(0x8003, bad, sizeof(bad));
        pub.onMessage(0x8002, bad, sizeof(bad));
        pub.onMessage(0x8001, NULL, 0);
        CHECK(line(readFile(rec), "state") == "none");

        std::string cmd = std::string("rm -rf ") + dir;
        if (system(cmd.c_str()) != 0) printf("(could not remove %s)\n", dir);
    }

    if (fails) { printf("test_media_info: %d failure(s)\n", fails); return 1; }
    printf("test_media_info OK\n");
    return 0;
}
