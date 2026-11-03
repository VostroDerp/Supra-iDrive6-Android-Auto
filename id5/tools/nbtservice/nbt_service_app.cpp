// libNbtService.so — a tiny iDrive PluginApp whose only jobs are to start the knockd SSH gate at boot and to offer
// the "SSH (15 min)" toggle in the iDrive menu. IMPROVEMENT-PLAN A1 (option a) + A5.
//
// Why it exists: today SSH depends on the Android Auto app. HmiMain3D loads NbtAad -> NbtAad sees
// /var/opt/aa/autostart -> it runs nbt-aad.sh -> the wrapper starts knockd. Anything that breaks that chain also
// removes SSH: a wrapper edit, autostart moved aside for a test, or the app's boot guard latching autostart off after
// three crash-boots. This plugin is loaded by HmiMain3D on its own and depends on none of that.
//
// Deliberately minimal, because a broken PluginApp can crash HmiMain3D at boot:
//   - nothing happens in create() beyond building the object;
//   - onInit() only registers handlers and posts two runnables;
//   - knockd is spawned from a posted runnable 2 s later, not on the app thread's first pass;
//   - no CDS subscriptions, no video, no sockets to anything but 127.0.0.1;
//   - /var/opt/aa/service.disable makes create() return NULL, and the non-executable knockd bit still disarms the
//     gate the way the boot wrapper's `[ -x ]` guard did.
// Recovery if it does break: it installs under /var/opt/hmi/data/ID5/Plugins/NbtService (A1a / F4), so `rm -r` over
// the still-working path is enough — nothing of ours is on the read-only system image.
//
// It does NOT stop knockd on onFinalize: the gate has to outlive the HMI, that is the whole point.
//
// ABI: the same trick as rhmiapp/nbt_aad_app.cpp — bmw::PluginApp is built with the framework's own constructor and
// gets its own copy of the vtable with onInit/onError/onFinalize replaced. See that file's header for the layout
// notes; this app only needs addHandler, post, updateModel(TextID/bool) and the base onInit/onFinalize.
//
// Protocol to knockd: UDP 127.0.0.1:<CTL_PORT from knock.conf> — "open <secs>", "close", "status". The checkbox and
// the status row are driven by the "status" reply, so iDrive always shows what the daemon actually has open, not what
// the plugin last asked for.
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <new>
#include <sys/stat.h>
#include <spawn.h>
#include <sys/wait.h>
#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)-2)   // QNX value
#endif
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef __QNX__
#include <sys/slog.h>
#include <sys/slogcodes.h>
#endif

#define NBT_SERVICE_VERSION "nbtservice-1 (" __DATE__ " " __TIME__ ")"

extern char** environ;

// ---- minimal declarations of BMW's RHMI app framework (libAppFramework.A.so / libCoreFramework.A.so) ------------
// Only names and parameter types matter (they produce the mangled symbols we import); nothing here is defined.
namespace bmw {
class IDispatcher;
class Bundle;
class Runnable;
class Parcel {
public:
    explicit Parcel(bool value);
    ~Parcel();
    void readBool(bool& value) const;
private:
    unsigned char m_storage[32];
};
enum RHMIInterfaceVersion { RHMI_INTERFACE_VERSION_1 = 1 };

template <typename T> class BasicString {
public:
    BasicString(const T* s);
    BasicString(const BasicString& other);
    ~BasicString();
    const T* raw() const { return m_data; }
private:
    T* m_data;
};

class TextID {
public:
    explicit TextID(unsigned id);
private:
    unsigned m_id;
    unsigned m_type;
};

template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate {
    void* object;
    R (*stub)(void* object, A1 a1);
};
typedef Delegate<bool, const Parcel&, void, void, void> ActionDelegate;

const size_t kPluginAppStorage = 0x200;

class PluginApp {
public:
    PluginApp(IDispatcher& dispatcher, BasicString<char> name, unsigned major, unsigned minor,
              RHMIInterfaceVersion rhmi, const Bundle& params, const Bundle& config);
    void addHandler(unsigned actionId, const ActionDelegate& handler);
    void addHandler(unsigned id, unsigned type, const ActionDelegate& handler);
    void post(Runnable* runnable, unsigned delayMs);
    void updateModel(unsigned modelId, const TextID& text);
    void updateModel(unsigned modelId, bool value);
    bool onInit();
    void onFinalize();
private:
    unsigned char m_storage[kPluginAppStorage];
};
} // namespace bmw

namespace {

// Paths. Fixed on the car; NBT_HOST_TEST lets the host test point them at a temp directory, the
// same way src/bt_play.cpp and src/voice_scenario.cpp build host stubs. The car binary is built
// without NBT_HOST_TEST, so it reads no environment at all.
char g_knockd[256]    = "/var/opt/aa/knockd";
char g_knockConf[256] = "/var/opt/aa/knock.conf";
char g_disable[256]   = "/var/opt/aa/service.disable";
char g_logPath[256]   = "/dev/shmem/nbt-service.log";
const long kLogMax    = 32768;                  // RAM: cap it, the HU runs for weeks

void initPaths() {
#ifdef NBT_HOST_TEST
    const char* dir = getenv("NBT_SERVICE_DIR");
    if (dir && *dir) {
        snprintf(g_knockd,    sizeof(g_knockd),    "%s/knockd", dir);
        snprintf(g_knockConf, sizeof(g_knockConf), "%s/knock.conf", dir);
        snprintf(g_disable,   sizeof(g_disable),   "%s/service.disable", dir);
        snprintf(g_logPath,   sizeof(g_logPath),   "%s/nbt-service.log", dir);
    }
#endif
}

// NbtService.xml ids. The entry row's placement is the ONE thing to change after the iDrive menu recon
// (NEXT-SESSION 0e): the insertStateID attribute in NbtService.xml. Nothing in this file depends on it.
const unsigned kMenuState     = 10;
const unsigned kActSsh        = 24;             // <checkbox action="24">
const unsigned kSshModel      = 34;             // raBoolModel: the tick
const unsigned kStatusModel   = 32;             // textIdModel of the status row
const unsigned kHandlerFocus  = 1;              // addHandler(stateId, 1, ...): focus, Parcel carries a bool

// Text DB ids (make_db.py).
const unsigned kTxtOff        = 20;             // "SSH: closed"
const unsigned kTxtOpen15     = 21;             // "SSH: open, 15 min left"
const unsigned kTxtOpen10     = 22;
const unsigned kTxtOpen5      = 23;
const unsigned kTxtOpen1      = 24;
const unsigned kTxtNoGate     = 25;             // "SSH: gate not running"
const unsigned kTxtStarting   = 26;             // "SSH: starting the gate..."

const unsigned kPollFocusMs   = 1000;           // our menu is on screen: live countdown
const unsigned kPollIdleMs    = 5000;
const unsigned kPollNoGateMs  = 15000;          // knockd is not answering: stop asking so often
const int      kArmTries      = 5;              // how often we try to start knockd at boot
const unsigned kArmRetryMs    = 30000;

// ---- log (capped; /dev/shmem is RAM and has no rename()) --------------------------------------------------------
void svcLogLine(const char* line) {
#ifdef __QNX__
    slogf(_SLOG_SETCODE(0x4e41, 0), _SLOG_INFO, "%s", line);
#else
    (void)line;
#endif
}

void svclog(const char* fmt, ...) {
    char msg[512];
    char ts[32];
    time_t t = time(NULL);
    struct stat st;
    FILE* f;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    svcLogLine(msg);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));
    bool over = (stat(g_logPath, &st) == 0 && (long)st.st_size >= kLogMax);
    f = fopen(g_logPath, over ? "w" : "a");
    if (!f) return;
    if (over) fprintf(f, "[%s] log truncated at %ld bytes\n", ts, (long)st.st_size);
    fprintf(f, "[%s] %s\n", ts, msg);
    fclose(f);
#ifndef __QNX__
    fprintf(stderr, "[nbtservice] %s\n", msg);
#endif
}

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

// ---- knock.conf: one source of truth for the control port and the window length -------------------------------
int  g_ctlPort = 52791;
long g_openS   = 900;

void readKnockConf() {
    FILE* f = fopen(g_knockConf, "r");
    char line[512];
    if (!f) { svclog("no %s: using CTL_PORT=%d OPEN_S=%ld", g_knockConf, g_ctlPort, g_openS); return; }
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') continue;
        if (!strncmp(p, "CTL_PORT=", 9)) {
            int v = atoi(p + 9);
            if (v > 0 && v < 65536) g_ctlPort = v;
        } else if (!strncmp(p, "OPEN_S=", 7)) {
            long v = atol(p + 7);
            if (v >= 10 && v <= 86400) g_openS = v;
        }
    }
    fclose(f);
    svclog("knock.conf: CTL_PORT=%d OPEN_S=%ld", g_ctlPort, g_openS);
}

// ---- talking to knockd ------------------------------------------------------------------------------------------
// One short request/reply on a connected UDP socket. Connected, so a knockd that is not running answers with
// ECONNREFUSED at once instead of making us wait for the timeout on the HMI's app thread.
// Timeouts: "status" replies within microseconds on loopback, but "open" can make knockd spawn sshd and wait up to
// half a second to see whether it survived, so a command gets a longer one.
const long kCtlStatusMs = 200;
const long kCtlCmdMs    = 1500;
bool knockdCtl(const char* cmd, char* reply, size_t replyLen, long timeoutMs) {
    struct sockaddr_in a;
    struct timeval tv;
    int s;
    ssize_t n;

    if (reply && replyLen) reply[0] = 0;
    if (g_ctlPort <= 0) return false;
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)g_ctlPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) { close(s); return false; }
    if (send(s, cmd, strlen(cmd), 0) < 0) { close(s); return false; }
    n = recv(s, reply, replyLen - 1, 0);
    close(s);
    if (n <= 0) return false;
    reply[n] = 0;
    return true;
}

int g_gateMisses = 0;                 // consecutive unanswered status probes

// Window state as knockd reports it.
struct Gate {
    bool running;      // the control port answered at all
    bool open;
    long left;         // seconds
};

Gate gateStatus() {
    Gate g;
    char reply[96];
    g.running = false;
    g.open = false;
    g.left = 0;
    if (!knockdCtl("status", reply, sizeof(reply), kCtlStatusMs)) { g_gateMisses++; return g; }
    g_gateMisses = 0;
    g.running = true;
    if (!strncmp(reply, "open", 4)) {
        g.open = true;
        g.left = atol(reply + 4);
        if (g.left < 0) g.left = 0;
    }
    return g;
}

// ---- starting knockd (A1) ---------------------------------------------------------------------------------------
// The x bit is the arm/disarm switch, as in the old boot wrapper: chmod +x arms, -x disarms.
bool knockdArmed() {
    return access(g_knockd, X_OK) == 0;
}

void spawnKnockd() {
    char* argv[3];
    char* envp[3];
    static char path[] = "PATH=/bin:/usr/bin:/opt/os/bin:/opt/sys/bin:/opt/core/bin:/opt/car/bin";
    int in, out;

    argv[0] = const_cast<char*>(g_knockd);
    argv[1] = const_cast<char*>(g_knockConf);
    argv[2] = NULL;
    envp[0] = path;
    envp[1] = NULL;

    in  = open("/dev/null", O_RDONLY);
    out = open("/dev/null", O_WRONLY);
    if (in < 0 || out < 0) {
        svclog("cannot open /dev/null (errno %d) — not starting knockd", errno);
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        return;
    }
#ifdef __QNX__
    {
        int fdmap[3];
        struct inheritance inh;
        pid_t pid;
        fdmap[0] = in; fdmap[1] = out; fdmap[2] = out;
        memset(&inh, 0, sizeof(inh));
        // NOZOMBIE: knockd's first process exits as soon as it has bound the port, and we must not leave a zombie
        // inside HmiMain3D. Whether the gate came up is checked through the control port, not through its exit code.
        inh.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_NOZOMBIE;
        inh.pgroup = SPAWN_NEWPGROUP;
        sigfillset(&inh.sigdefault);
        sigemptyset(&inh.sigmask);
        pid = spawn(g_knockd, 3, fdmap, &inh, argv, envp);
        if (pid < 0) svclog("spawn %s failed (errno %d)", g_knockd, errno);
        else svclog("started %s %s (pid %d)", g_knockd, g_knockConf, (int)pid);
    }
#else
    {
        posix_spawn_file_actions_t fa;
        pid_t pid = -1;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, in, 0);
        posix_spawn_file_actions_adddup2(&fa, out, 1);
        posix_spawn_file_actions_adddup2(&fa, out, 2);
        if (posix_spawn(&pid, g_knockd, &fa, NULL, argv, envp) != 0) pid = -1;
        posix_spawn_file_actions_destroy(&fa);
        if (pid < 0) svclog("spawn %s failed (errno %d)", g_knockd, errno);
        else { svclog("started %s %s (pid %d)", g_knockd, g_knockConf, (int)pid); waitpid(pid, NULL, 0); }
    }
#endif
    close(in);
    close(out);
}

// ---- the app ----------------------------------------------------------------------------------------------------
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };

void* g_vtbl[2 + kVtblSlots];
bmw::PluginApp* g_app = NULL;         // the object the framework holds
bmw::PluginApp* g_runApp = NULL;      // non-NULL between onInit and onFinalize: safe to touch models
bool     g_menuFocused = false;
int      g_armTries = 0;
bool     g_gateSeen = false;          // knockd has answered at least once
unsigned g_shownStatus = 0;           // TextID currently on the status row
int      g_shownTick = -1;            // last checkbox value pushed (-1 = none)

struct FakeRunnable { void** vptr; };
void runnableNoop(void*) {}

FakeRunnable g_pollRunnable;
void* g_pollVtbl[5];
FakeRunnable g_armRunnable;
void* g_armVtbl[5];

void initRunnable(FakeRunnable& r, void** vtbl, void (*run)(void*)) {
    void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
    if (base) memcpy(vtbl, base, 5 * sizeof(void*));
    else { vtbl[0] = NULL; vtbl[1] = NULL; }
    vtbl[2] = reinterpret_cast<void*>(&runnableNoop);      // ~Runnable
    vtbl[3] = reinterpret_cast<void*>(&runnableNoop);      // deleting ~Runnable
    vtbl[4] = reinterpret_cast<void*>(run);                // run()
    r.vptr = vtbl + 2;
}

unsigned statusText(const Gate& g) {
    // knockd is single-threaded: while it spawns sshd it can miss one probe. Only call the gate down after two in
    // a row, so the row does not flicker through "not running" every time the window opens.
    if (!g.running) {
        if (g_gateMisses < 2 && g_shownStatus) return g_shownStatus;
        return knockdArmed() ? (g_gateSeen ? kTxtNoGate : kTxtStarting) : kTxtNoGate;
    }
    if (!g.open) return kTxtOff;
    if (g.left > 600) return kTxtOpen15;
    if (g.left > 300) return kTxtOpen10;
    if (g.left > 60)  return kTxtOpen5;
    return kTxtOpen1;
}

void showGate(bmw::PluginApp* app, const Gate& g) {
    unsigned want = statusText(g);
    int tick = g.open ? 1 : 0;
    if (want != g_shownStatus) {
        bmw::TextID text(want);
        app->updateModel(kStatusModel, text);
        g_shownStatus = want;
        svclog("status row -> TextID %u (gate %s, %s, %ld s left)", want,
             g.running ? "up" : "down", g.open ? "open" : "closed", g.left);
    }
    if (tick != g_shownTick) {
        app->updateModel(kSshModel, g.open);
        g_shownTick = tick;
    }
}

void pollRun(void*) {
    if (!g_runApp) return;
    Gate g = gateStatus();
    if (g.running) g_gateSeen = true;
    showGate(g_runApp, g);
    unsigned next = !g.running ? kPollNoGateMs : (g_menuFocused || g.open ? kPollFocusMs : kPollIdleMs);
    g_runApp->post(reinterpret_cast<bmw::Runnable*>(&g_pollRunnable), next);
}

// Boot arming, A1: try a few times, 30 s apart, and stop as soon as the control port answers. Two knockds racing is
// harmless anyway (the second fails to bind and exits), but there is no reason to keep spawning them.
void armRun(void*) {
    if (!g_runApp) return;
    Gate g = gateStatus();
    if (g.running) {
        g_gateSeen = true;
        svclog("knockd is answering on 127.0.0.1:%d — nothing to do (%s)", g_ctlPort,
             g_armTries ? "started by us" : "already running, e.g. from the AA boot wrapper");
        return;
    }
    if (!knockdArmed()) {
        svclog("%s missing or not executable — gate disarmed on purpose? (chmod +x to arm)", g_knockd);
        return;                                            // deliberate state: do not retry
    }
    if (g_armTries >= kArmTries) {
        svclog("knockd still not answering after %d attempts — giving up (see /var/opt/aa/knock.log)", g_armTries);
        return;
    }
    g_armTries++;
    svclog("knockd not answering: start attempt %d of %d", g_armTries, kArmTries);
    spawnKnockd();
    g_runApp->post(reinterpret_cast<bmw::Runnable*>(&g_armRunnable), kArmRetryMs);
}

// ---- menu handlers ----------------------------------------------------------------------------------------------
// A5: being in the car is the authorization, so the toggle opens the window with no knock. sshd itself stays
// key-only either way. The checkbox is set from what knockd reports, never from what we asked for.
bool onSshToggle(void*, const bmw::Parcel&) {
    Gate g = gateStatus();
    char cmd[64];
    char reply[96];
    if (!g.running) {
        svclog("SSH toggle pressed but the gate is not running: trying to start it");
        if (knockdArmed()) { spawnKnockd(); }
        else svclog("%s is not executable — nothing to start", g_knockd);
    } else if (g.open) {
        knockdCtl("close", reply, sizeof(reply), kCtlCmdMs);
        svclog("SSH toggle -> close (knockd: %s)", reply);
    } else {
        snprintf(cmd, sizeof(cmd), "open %ld", g_openS);
        knockdCtl(cmd, reply, sizeof(reply), kCtlCmdMs);
        svclog("SSH toggle -> %s (knockd: %s)", cmd, reply);
    }
    if (g_runApp) {
        Gate now = gateStatus();
        if (now.running) g_gateSeen = true;
        showGate(g_runApp, now);
    }
    return true;
}

bool onMenuFocus(void*, const bmw::Parcel& p) {
    bool f = false;
    p.readBool(f);
    g_menuFocused = f;
    if (f && g_runApp) {
        Gate g = gateStatus();
        showGate(g_runApp, g);
    }
    return true;
}

void addAction(bmw::PluginApp* self, unsigned id, bool (*stub)(void*, const bmw::Parcel&)) {
    bmw::ActionDelegate d;
    d.object = self;
    d.stub = stub;
    self->addHandler(id, d);
}

bool appOnInit(bmw::PluginApp* self) {
    bool ok;
    addAction(self, kActSsh, &onSshToggle);
    {
        bmw::ActionDelegate f;
        f.object = self;
        f.stub = &onMenuFocus;
        self->addHandler(kMenuState, kHandlerFocus, f);
    }
    ok = self->onInit();
    g_runApp = self;
    readKnockConf();
    initRunnable(g_armRunnable, g_armVtbl, &armRun);
    initRunnable(g_pollRunnable, g_pollVtbl, &pollRun);
    self->post(reinterpret_cast<bmw::Runnable*>(&g_armRunnable), 2000);
    self->post(reinterpret_cast<bmw::Runnable*>(&g_pollRunnable), 3000);
    svclog("onInit: raAction %u + focus on state %u, base onInit -> %d, knockd %s", kActSsh, kMenuState,
         (int)ok, knockdArmed() ? "armed (+x)" : "disarmed (not executable)");
    return ok;
}

void appOnError(bmw::PluginApp*, unsigned code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    svclog("onError %u: %s", code, s ? s : "(null)");
}

void appOnFinalize(bmw::PluginApp* self) {
    svclog("onFinalize (knockd keeps running on purpose)");
    g_runApp = NULL;
    self->onFinalize();
}

bmw::PluginApp* createApp(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params,
                          const bmw::Bundle& config) {
    void* mem;
    bmw::PluginApp* app;
    void** base;

    svclog("create(name=%s) %s", name ? name : "(null)", NBT_SERVICE_VERSION);
    if (fileExists(g_disable)) { svclog("disabled by %s", g_disable); return NULL; }
    if (g_app) { svclog("create: already created"); return NULL; }

    mem = ::operator new(bmw::kPluginAppStorage);
    memset(mem, 0, bmw::kPluginAppStorage);
    app = new (mem) bmw::PluginApp(dispatcher, bmw::BasicString<char>(name ? name : "NbtService"), 1, 0,
                                   bmw::RHMI_INTERFACE_VERSION_1, params, config);

    base = *reinterpret_cast<void***>(app);
    memcpy(g_vtbl, base - 2, sizeof(g_vtbl));
    g_vtbl[2 + kSlotOnInit]     = reinterpret_cast<void*>(&appOnInit);
    g_vtbl[2 + kSlotOnError]    = reinterpret_cast<void*>(&appOnError);
    g_vtbl[2 + kSlotOnFinalize] = reinterpret_cast<void*>(&appOnFinalize);
    *reinterpret_cast<void***>(app) = g_vtbl + 2;

    g_app = app;
    return app;
}

void destroyApp(void* app) {
    svclog("destroy(%p)", app);
    if (!app) return;
    {
        void** vt = *reinterpret_cast<void***>(app);
        reinterpret_cast<void (*)(void*)>(vt[1])(app);     // deleting destructor
    }
    if (app == g_app) g_app = NULL;
}

__attribute__((constructor)) void libraryLoaded() {
    initPaths();
    svclog("library loaded into pid %d, %s", (int)getpid(), NBT_SERVICE_VERSION);
}

} // namespace

// ---- entry point looked up by ManagedPluginApp::init (dlsym "pluginAppInfo") ------------------------------------
extern "C" {
struct NbtPluginAppInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    const char* name;
    const char* vendor;
    uint32_t rhmiInterfaceVersion;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) NbtPluginAppInfo pluginAppInfo = {
    1, 0, "NBT Service", "NBT-AAD-ID5", 1, &createApp, &destroyApp
};
}
