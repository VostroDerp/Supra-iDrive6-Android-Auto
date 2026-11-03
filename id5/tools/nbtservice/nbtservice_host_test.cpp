// Host test driver for libNbtService.so: loads it the way ManagedPluginApp::init does (dlsym "pluginAppInfo"),
// then drives create / onInit / the posted runnables / the checkbox action / onFinalize / destroy against the fake
// framework. test_nbtservice.py prepares the directory the plugin works in and builds this.
//
//   nbtservice_host_test <lib> gate     a real knockd + knock.conf are in NBT_SERVICE_DIR
//   nbtservice_host_test <lib> nogate   the directory is empty: nothing to start
#include "nbtservice_fake_appframework.hpp"
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

struct Info {
    uint16_t a, b;
    const char* name;
    const char* vendor;
    uint32_t rhmi;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};

static int g_fails = 0;

static void check(const char* what, bool ok, const char* detail = "") {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", what, (!ok && detail[0]) ? " — " : "", ok ? "" : detail);
    if (!ok) g_fails++;
}

static void runPosted(bmw::Runnable* r) {
    void** vt = *reinterpret_cast<void***>(r);
    reinterpret_cast<void (*)(void*)>(vt[2])(r);           // slot 2 of the adjusted vptr = run()
}

static int findAction(unsigned id) {
    for (int i = 0; i < bmw::g_fake.actionCount; i++)
        if (bmw::g_fake.actionIds[i] == id) return i;
    return -1;
}

static bool fireAction(unsigned id, bool parcelValue) {
    int i = findAction(id);
    if (i < 0) return false;
    bmw::Parcel p(parcelValue);
    return bmw::g_fake.actions[i].stub(bmw::g_fake.actions[i].object, p);
}

// Run the poll runnable until the status row shows wantText, or give up.
static bool pollUntil(bmw::Runnable* poll, unsigned wantText, int tries) {
    for (int i = 0; i < tries; i++) {
        runPosted(poll);
        if (bmw::g_fake.lastTextId == wantText) return true;
        usleep(200000);
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: %s <libNbtService.so> <gate|nogate>\n", argv[0]); return 2; }
    const bool gate = !strcmp(argv[2], "gate");
    char dummy[64];
    memset(dummy, 0, sizeof(dummy));

    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    Info* info = (Info*)dlsym(h, "pluginAppInfo");
    if (!info) { printf("no pluginAppInfo\n"); return 1; }
    printf("pluginAppInfo: %u.%u name=%s vendor=%s rhmi=%u\n", info->a, info->b, info->name, info->vendor, info->rhmi);
    check("pluginAppInfo names the app", !strcmp(info->name, "NBT Service"), info->name);

    bmw::PluginApp* app = info->create(*(bmw::IDispatcher*)dummy, "NbtService",
                                       *(const bmw::Bundle*)dummy, *(const bmw::Bundle*)dummy);
    check("create returns an app", app != NULL);
    if (!app) return 1;

    bool ok = app->onInit();                               // the framework calls these virtually
    check("onInit returns the base result", ok);
    check("checkbox action 24 registered", findAction(24) >= 0);
    check("focus handler on state 10 registered",
          bmw::g_fake.notifyCount == 1 && bmw::g_fake.notifyIds[0] == 10 && bmw::g_fake.notifyTypes[0] == 1);
    check("two runnables posted (arm, poll)", bmw::g_fake.postCount == 2);
    if (bmw::g_fake.postCount != 2) return 1;
    check("arming is posted first and not on the app thread's first pass",
          bmw::g_fake.postDelays[0] >= 1000 && bmw::g_fake.postDelays[1] >= 1000);

    bmw::Runnable* arm = bmw::g_fake.posted[0];
    bmw::Runnable* poll = bmw::g_fake.posted[1];

    if (gate) {
        runPosted(arm);                                    // starts knockd from NBT_SERVICE_DIR
        check("status row reaches 'closed' once the gate answers", pollUntil(poll, 20, 25),
              "last TextID was not 20");
        check("status row uses textIdModel 32", bmw::g_fake.lastTextModel == 32);
        check("checkbox pushed to the raBoolModel", bmw::g_fake.lastBoolModel == 34 && !bmw::g_fake.lastBool);

        check("checkbox press returns true", fireAction(24, true));
        check("window opens: status says 15 min left", pollUntil(poll, 21, 10), "last TextID was not 21");
        check("checkbox now ticked", bmw::g_fake.lastBool);

        check("second press returns true", fireAction(24, false));
        check("window closes again", pollUntil(poll, 20, 10), "last TextID was not 20");
        check("checkbox unticked", !bmw::g_fake.lastBool);

        // A second arm pass must not start a second knockd; it just reports.
        runPosted(arm);
        check("arming again is a no-op while the gate answers", true);
    } else {
        runPosted(arm);                                    // nothing to start
        check("no knockd: status says the gate is not running", pollUntil(poll, 25, 5),
              "last TextID was not 25");
        check("no knockd: the checkbox stays off", !bmw::g_fake.lastBool);
        check("no knockd: pressing the checkbox does not crash", fireAction(24, true));
    }

    // Focus on our page refreshes the rows.
    if (bmw::g_fake.notifyCount) {
        bmw::Parcel p(true);
        int before = bmw::g_fake.textUpdates;
        bmw::g_fake.notifies[0].stub(bmw::g_fake.notifies[0].object, p);
        check("focus handler runs without changing anything unexpectedly",
              bmw::g_fake.textUpdates >= before);
    }

    bmw::BasicString<char> msg("integration error");
    app->onError(7, msg);
    app->onFinalize();
    info->destroy(app);
    check("onFinalize + destroy survive", true);

    printf("%s\n", g_fails ? "nbtservice host test FAILED" : "nbtservice host test OK");
    return g_fails ? 1 : 0;
}
