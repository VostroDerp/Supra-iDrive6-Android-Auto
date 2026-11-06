// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the iDrive app's now playing logic (rhmiapp/now_playing.hpp). Reads records made by the daemon's own
// writer (mediaRecord, src/media_info.hpp), torn records, change detection, the cluster rows and their commands, the
// art check, times, UTF-16 and the flag file options.
#include "media_info.hpp"
#include "../rhmiapp/now_playing.hpp"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace nbt;
using namespace nbt::nowplaying;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static MediaNowPlaying sample() {
    MediaNowPlaying np;
    np.status.state = 2; np.status.source = "Spotify"; np.status.position = 75;
    np.meta.song = "Lightyears"; np.meta.artist = "Ente"; np.meta.album = "First"; np.meta.playlist = "Mix";
    np.meta.duration = 215;
    np.artSeq = 3; np.art.type = MEDIA_ART_PNG; np.art.width = 300; np.art.height = 300; np.artBytes = 1234;
    return np;
}

int main() {
    // 1. Round trip with the daemon's writer.
    {
        const std::string r = mediaRecord(sample(), 9, "/dev/shmem/nbt-aad-art");
        Track t;
        CHECK(parseRecord(r.data(), r.size(), t));
        CHECK(t.seq == 9 && t.state == "playing" && t.app == "Spotify" && t.song == "Lightyears");
        CHECK(t.artist == "Ente" && t.album == "First" && t.playlist == "Mix");
        CHECK(t.duration == 215 && t.position == 75);
        CHECK(t.artSeq == 3 && t.artType == "png" && t.artBytes == 1234 && t.artW == 300 && t.artH == 300);
        CHECK(t.artPath == "/dev/shmem/nbt-aad-art");
        CHECK(hasSong(t));
    }
    // 2. Empty fields, no art, session ended.
    {
        MediaNowPlaying np;
        const std::string r = mediaRecord(np, 1, "/dev/shmem/nbt-aad-art");
        Track t;
        t.song = "old";
        CHECK(parseRecord(r.data(), r.size(), t));
        CHECK(t.state == "none" && t.song.empty() && t.app.empty() && t.artType == "none" && t.artPath == "-");
        CHECK(!hasSong(t) && clusterRows(t).empty());
    }
    // 3. Torn or foreign records keep what was shown.
    {
        std::string r = mediaRecord(sample(), 12, "/a");
        Track t;
        t.song = "kept";
        std::string torn = r;
        torn.replace(torn.find("end 12"), 6, "end 11");             // writer half way through
        CHECK(!parseRecord(torn.data(), torn.size(), t) && t.song == "kept");
        CHECK(!parseRecord(r.data(), 100, t) && t.song == "kept");   // cut short: no end line
        std::string v2 = r;
        v2[14] = '2';                                                // "nbt-aad-media 2": unknown format
        CHECK(!parseRecord(v2.data(), v2.size(), t));
        CHECK(!parseRecord(NULL, 0, t) && !parseRecord("", 0, t));
        // A newer daemon's extra line is skipped.
        std::string extra = r;
        extra.insert(extra.find("\nduration"), "\nrating 5");
        CHECK(parseRecord(extra.data(), extra.size(), t) && t.duration == 215);
    }
    // 4. Changes.
    {
        Track a, b;
        CHECK(changes(a, false, b) == (unsigned)(kText | kArt | kProgress | kState));
        CHECK(changes(a, true, b) == 0);
        b.position = 1;
        CHECK(changes(a, true, b) == (unsigned)kProgress);
        b = a; b.song = "x";
        CHECK(changes(a, true, b) == (unsigned)kText);
        b = a; b.app = "YouTube Music";
        CHECK(changes(a, true, b) == (unsigned)kText);
        b = a; b.artSeq = 1;
        CHECK(changes(a, true, b) == (unsigned)kArt);
        b = a; b.state = "paused";
        CHECK(changes(a, true, b) == (unsigned)kState);
    }
    // 5. Cluster rows and what a row press sends.
    {
        Track t;
        t.state = "paused"; t.song = "Song"; t.artist = "Artist"; t.album = "Album";
        const std::vector<ClusterRow> rows = clusterRows(t);
        CHECK(rows.size() == 3);
        CHECK(rows[0].index == 0 && rows[0].title == kPrevRow && !rows[0].checked && rows[0].artist.empty());
        CHECK(rows[1].index == 1 && rows[1].title == "Song" && rows[1].artist == "Artist" && rows[1].album == "Album"
              && rows[1].checked);
        CHECK(rows[2].index == 2 && rows[2].title == kNextRow && !rows[2].checked && rows[2].album.empty());
        CHECK(!strcmp(commandForRow(0), "prev") && !strcmp(commandForRow(1), "play") && !strcmp(commandForRow(2), "next"));
        CHECK(commandForRow(3) == NULL && commandForRow(-1) == NULL);
        CHECK(mediaKeyFor(commandForRow(0)) == 88 && mediaKeyFor(commandForRow(2)) == 87 && mediaKeyFor(commandForRow(1)) == 126);
        const uint8_t two[] = { 0, 0, 0, 2 };
        const uint8_t big[] = { 0xFF, 0xFF, 0xFF, 0xFF };
        CHECK(rowFromParcel(two, 4) == 2 && rowFromParcel(two, 3) == -1 && rowFromParcel(big, 4) == -1);
        CHECK(rowFromParcel(NULL, 0) == -1);
    }
    // 6. The art check.
    {
        Track t;
        t.artType = "png"; t.artBytes = 12;
        const uint8_t png[12] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13 };
        CHECK(artMatches(png, 12, t));
        CHECK(!artMatches(png, 11, t));                              // next art half written
        t.artType = "jpeg";
        CHECK(!artMatches(png, 12, t));
        const uint8_t jpg[4] = { 0xFF, 0xD8, 0xFF, 0xE0 };
        t.artBytes = 4;
        CHECK(artMatches(jpg, 4, t));
        t.artType = "other";
        CHECK(!artMatches(jpg, 4, t));
    }
    // 7. Times and progress.
    {
        CHECK(formatTime(0) == "0:00" && formatTime(75) == "1:15" && formatTime(3725) == "1:02:05");
        Track t;
        CHECK(progressPercent(t) == 0);
        t.duration = 200; t.position = 50;
        CHECK(progressPercent(t) == 25);
        t.position = 500;
        CHECK(progressPercent(t) == 100);
    }
    // 8. UTF-16.
    {
        std::vector<unsigned short> u = utf16("A\xc3\xa9\xe2\x82\xac\xf0\x9f\x8e\xb5");   // A, e-acute, euro, U+1F3B5
        CHECK(u.size() == 6 && u[0] == 'A' && u[1] == 0xE9 && u[2] == 0x20AC && u[3] == 0xD83C && u[4] == 0xDFB5 && u[5] == 0);
        u = utf16("a\xff" "b\xc3");                                  // invalid byte, cut sequence
        CHECK(u.size() == 5 && u[1] == 0xFFFD && u[2] == 'b' && u[3] == 0xFFFD && u[4] == 0);
        u = utf16("\xc0\xaf");                                       // overlong '/'
        CHECK(u[0] == 0xFFFD);
        CHECK(utf16("").size() == 1);
    }
    // 9. Flag file options.
    {
        Options o = parseOptions(NULL);
        CHECK(o.stringType == 0x22 && !o.listTypesKnown());
        o = parseOptions("1\n");                                    // the plain "on" flag
        CHECK(!o.listTypesKnown());
        o = parseOptions("int-type 0x05\nbool-type 7\nstring-type 34\n");
        CHECK(o.intType == 5 && o.boolType == 7 && o.stringType == 34 && o.listTypesKnown());
        o = parseOptions("int-type -3\nbool-type x");
        CHECK(o.intType == 0 && o.boolType == 0);
    }

    if (fails) { printf("test_now_playing: %d failure(s)\n", fails); return 1; }
    printf("test_now_playing OK\n");
    return 0;
}
