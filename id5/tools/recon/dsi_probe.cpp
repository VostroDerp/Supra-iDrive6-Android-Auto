// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// dsi_probe: is a DSI service name taken? Opens each name as a CLIENT (never registers a server) and reports
// whether a server attached within the wait. Read-only towards the car: a client only listens, sends nothing.
//
//   dsi_probe <secs> <Role.Interface:major.minor> [...]
//   dsi_probe 5 MOSTSubSystem.DSYSMOSTServer:4.5     the control: served on every NBT (test_controls uses it)
//   Other names and versions come from the binaries that serve them (strings, --map arguments).
//
// ATTACHED = a server holds that name (a second server with it would be refused by the broker).
// no attach = nobody serves it, or the version does not match: read the name and version from the binary that
// serves it. Build (from milestone-7, QNX docker image):
//   qcc -Vgcc_ntoarmv7le_cpp -O2 -Wall -D_QNX_SOURCE -Wc,-std=gnu++0x -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp
//       -Iinclude -Isrc -o dsi_probe ../id5/tools/recon/dsi_probe.cpp src/dsi_client.cpp
#include "dsi_client.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {
const int kMax = 8;
volatile int g_attached[kMax];
char g_names[kMax][128];

void onAttached(nbt::DsiClient*, void* user) {
    const int i = (int)(long)user;
    g_attached[i] = 1;
    printf("ATTACHED %s\n", g_names[i]);
    fflush(stdout);
}
void onData(nbt::DsiClient*, uint32_t type, uint32_t id, uint32_t, const uint8_t*, uint32_t len, void* user) {
    printf("  data from %s: type %u id 0x%x, %u bytes\n", g_names[(int)(long)user], type, id, len);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: dsi_probe <secs> <Role.Interface:major.minor> [...] (max %d)\n", kMax);
        return 2;
    }
    const int secs = atoi(argv[1]);
    int n = 0;
    for (int a = 2; a < argc && n < kMax; ++a) {
        char role[64], iface[64];
        int major = 0, minor = 0;
        const char* dot = strchr(argv[a], '.');
        const char* colon = strchr(argv[a], ':');
        if (!dot || !colon || colon < dot || dot - argv[a] >= 64 || colon - dot - 1 >= 64 ||
            sscanf(colon + 1, "%d.%d", &major, &minor) != 2) {
            fprintf(stderr, "bad name %s (want Role.Interface:major.minor)\n", argv[a]);
            continue;
        }
        memcpy(role, argv[a], dot - argv[a]);
        role[dot - argv[a]] = 0;
        memcpy(iface, dot + 1, colon - dot - 1);
        iface[colon - dot - 1] = 0;
        snprintf(g_names[n], sizeof(g_names[n]), "%s.%s %d.%d", role, iface, major, minor);
        nbt::DsiClient* c = new nbt::DsiClient();
        c->setQuiet(true);
        c->setCallbacks(&onAttached, &onData, (void*)(long)n);
        if (!c->open(iface, role, major, minor)) printf("OPEN FAILED %s\n", g_names[n]);
        ++n;
    }
    for (int t = 0; t < secs * 10; ++t) usleep(100000);
    printf("--- after %d s\n", secs);
    for (int i = 0; i < n; ++i) printf("%-10s %s\n", g_attached[i] ? "ATTACHED" : "no attach", g_names[i]);
    fflush(stdout);
    _exit(0);                                    // drop the connections, no orderly teardown needed
}
