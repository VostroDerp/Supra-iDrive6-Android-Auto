// desk_rig: runs NBT-AAD's own Android Auto session (src/aa_session.cpp, unmodified) against a real phone on a
// Linux laptop's USB port, so phone-side behaviour can be tested at the desk instead of in the car.
//
// What it does, in the order the car does it (usb_direct.cpp):
//   1. Finds the phone (an ADB or MTP/PTP interface), asks its AOA protocol (request 51), sends the daemon's own
//      accessory identity (usb_aoap.hpp fillIdentity, request 52) and starts accessory mode (request 53).
//   2. Waits for the phone to come back as 18d1:2d00..2d05 and claims its accessory interface (not the adb one).
//   3. Bridges bulk IN/OUT to a socketpair, writing one AA frame per bulk transfer (usb_aoap.hpp frameLength),
//      with a zero-length packet after a transfer that is a whole number of packets, like UsbDirectLink::write().
//   4. Runs AASession on the other end of the socketpair and records what the phone does: video to
//      <out>/video.h264, counts of video frames / audio per channel / focus requests.
//
// What it cannot test: the QNX side (io-usb, the driver-path claim, devc-serusb-aoap / NbtMediaMainApp stealing
// transfers), and car I/O (screen, touch, knob, car audio). See README.md.
//
// NBT_* settings work as on the car (AASession reads them), e.g. NBT_SSL_CIPHERS=..., NBT_VERBOSE=1.

#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <libusb-1.0/libusb.h>

#include "aa_session.hpp"
#include "usb_aoap.hpp"

namespace {

const uint16_t kGoogleVendor = 0x18d1;
const uint16_t kFirstAccessoryPid = 0x2d00;
const uint16_t kLastAccessoryPid = 0x2d05;
const uint8_t kAoaGetProtocol = 51, kAoaSendString = 52, kAoaStart = 53, kAoaSetAudioMode = 58;
const size_t kRxBuf = 16384;   // like devc-serusb-aoap -l 16384 / UsbDirect kRxBuf
const size_t kTxBuf = 65536;   // UsbDirect kTxBuf

struct Options {
    int seconds = 60;
    int protocol = 1;            // identity.protocol, as the daemon sends it (patched builds use 2)
    bool audio_mode = false;     // AOA2 SET_AUDIO_MODE before START
    const char* manufacturer = NULL;
    const char* model = NULL;
    int vid = -1, pid = -1;      // --device VID:PID
    std::string out = "build/out";
    bool list_only = false;
};

volatile sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

double now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void rigLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void rigLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[DeskRig] ");
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);
}

bool isAccessory(const libusb_device_descriptor& d) {
    return d.idVendor == kGoogleVendor && d.idProduct >= kFirstAccessoryPid && d.idProduct <= kLastAccessoryPid;
}

// A phone in normal mode shows an ADB (ff/42/01) or MTP/PTP (06/01/01, or ff/ff/00 "MTP" on some phones)
// interface. Only those are asked for their AOA protocol: no vendor requests to keyboards, hubs or webcams.
bool looksLikePhone(libusb_device* dev) {
    libusb_config_descriptor* cfg = NULL;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0 || !cfg) return false;
    bool phone = false;
    for (int i = 0; i < cfg->bNumInterfaces && !phone; ++i) {
        for (int a = 0; a < cfg->interface[i].num_altsetting && !phone; ++a) {
            const libusb_interface_descriptor& id = cfg->interface[i].altsetting[a];
            if (id.bInterfaceClass == 0xff && id.bInterfaceSubClass == 0x42) phone = true;     // adb
            if (id.bInterfaceClass == 0x06 && id.bInterfaceSubClass == 0x01) phone = true;     // MTP / PTP
            if (id.bInterfaceClass == 0xff && id.bInterfaceSubClass == 0xff && id.bInterfaceProtocol == 0x00 &&
                id.bNumEndpoints == 3) phone = true;                                            // vendor MTP
        }
    }
    libusb_free_config_descriptor(cfg);
    return phone;
}

std::string devName(libusb_device* dev, const libusb_device_descriptor& d) {
    char b[64];
    snprintf(b, sizeof(b), "%04x:%04x (bus %d dev %d)", d.idVendor, d.idProduct, libusb_get_bus_number(dev),
             libusb_get_device_address(dev));
    return b;
}

// First matching device: an accessory if accessory == true, else a phone in normal mode.
libusb_device* findDevice(libusb_context* ctx, const Options& o, bool accessory, bool verbose) {
    libusb_device** list = NULL;
    ssize_t n = libusb_get_device_list(ctx, &list);
    libusb_device* found = NULL;
    int candidates = 0;
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device_descriptor d;
        if (libusb_get_device_descriptor(list[i], &d) != 0) continue;
        bool match;
        if (accessory) match = isAccessory(d);
        else if (o.vid >= 0) match = d.idVendor == o.vid && d.idProduct == o.pid;
        else match = !isAccessory(d) && looksLikePhone(list[i]);
        if (!match) continue;
        ++candidates;
        if (verbose) rigLog("%s %s", accessory ? "accessory" : "phone candidate", devName(list[i], d).c_str());
        if (!found) found = libusb_ref_device(list[i]);
    }
    libusb_free_device_list(list, 1);
    if (!accessory && candidates > 1 && o.vid < 0) {
        rigLog("%d phone candidates: pick one with --device VID:PID", candidates);
        libusb_unref_device(found);
        return NULL;
    }
    return found;
}

bool sendString(libusb_device_handle* h, uint16_t index, const char* s) {
    int r = libusb_control_transfer(h, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR, kAoaSendString, 0, index,
                                    (unsigned char*)s, (uint16_t)(strlen(s) + 1), 1000);
    if (r < 0) rigLog("SEND_STRING %u failed: %s", index, libusb_error_name(r));
    return r >= 0;
}

// Steps 1: AOA protocol, identity, start. The phone then drops off the bus.
bool switchToAccessory(libusb_context* ctx, const Options& o) {
    libusb_device* dev = findDevice(ctx, o, false, true);
    if (!dev) {
        rigLog("no phone found (USB debugging or file transfer on? cable data-capable? --list shows what is seen)");
        return false;
    }
    libusb_device_descriptor d;
    libusb_get_device_descriptor(dev, &d);
    libusb_device_handle* h = NULL;
    int r = libusb_open(dev, &h);
    libusb_unref_device(dev);
    if (r != 0) {
        rigLog("open %04x:%04x failed: %s%s", d.idVendor, d.idProduct, libusb_error_name(r),
             r == LIBUSB_ERROR_ACCESS ? " (permissions: see README.md, udev rule)" : "");
        return false;
    }
    unsigned char ver[2] = {0, 0};
    r = libusb_control_transfer(h, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR, kAoaGetProtocol, 0, 0, ver, 2,
                                1000);
    int proto = r == 2 ? (ver[0] | ver[1] << 8) : 0;
    rigLog("phone AOA protocol: %d%s", proto, proto ? "" : " (no accessory support, or the request failed)");
    if (!proto) { libusb_close(h); return false; }

    nbt::aoap::Identity id;
    nbt::aoap::fillIdentity(id, o.manufacturer, o.model);
    id.protocol = o.protocol;
    rigLog("identity: manufacturer '%s' model '%s' description '%s' version '%s' serial '%s' protocol %d",
         id.manufacturer, id.model, id.description, id.version, id.serial, id.protocol);
    bool ok = sendString(h, 0, id.manufacturer) && sendString(h, 1, id.model) && sendString(h, 2, id.description) &&
              sendString(h, 3, id.version) && sendString(h, 4, id.url) && sendString(h, 5, id.serial);
    if (ok && o.audio_mode) {
        if (proto < 2) rigLog("--aoa-audio needs AOA protocol 2, the phone has %d: skipped", proto);
        else {
            r = libusb_control_transfer(h, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR, kAoaSetAudioMode, 1, 0,
                                        NULL, 0, 1000);
            rigLog("SET_AUDIO_MODE 1: %s", r < 0 ? libusb_error_name(r) : "ok");
        }
    }
    if (ok) {
        r = libusb_control_transfer(h, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR, kAoaStart, 0, 0, NULL, 0,
                                    1000);
        ok = r >= 0;
        rigLog("switch to accessory mode sent as '%s' / '%s'%s", id.manufacturer, id.model,
             ok ? "" : " (START failed)");
    }
    libusb_close(h);
    return ok;
}

struct Link {
    libusb_device_handle* h = NULL;
    int iface = -1;
    uint8_t ep_in = 0, ep_out = 0;
    int max_packet_out = 512;
    int sock = -1;                  // our end of the socketpair
    volatile bool done = false;
    bool zlp_ok = true;
    uint64_t rx_bytes = 0, tx_bytes = 0, tx_frames = 0;
};

// Accessory interface: bulk IN + OUT, not the adb interface (subclass 0x42) of 2d01/2d03/2d05.
bool openAccessory(libusb_device* dev, Link& L) {
    libusb_config_descriptor* cfg = NULL;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0) return false;
    for (int i = 0; i < cfg->bNumInterfaces && L.iface < 0; ++i) {
        const libusb_interface_descriptor& id = cfg->interface[i].altsetting[0];
        if (id.bInterfaceSubClass == 0x42) continue;
        uint8_t in = 0, out = 0;
        int mp = 512;
        for (int e = 0; e < id.bNumEndpoints; ++e) {
            const libusb_endpoint_descriptor& ep = id.endpoint[e];
            if ((ep.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK) continue;
            if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) in = ep.bEndpointAddress;
            else { out = ep.bEndpointAddress; mp = ep.wMaxPacketSize ? ep.wMaxPacketSize : 512; }
        }
        if (in && out) { L.iface = id.bInterfaceNumber; L.ep_in = in; L.ep_out = out; L.max_packet_out = mp; }
    }
    libusb_free_config_descriptor(cfg);
    if (L.iface < 0) { rigLog("accessory has no bulk interface"); return false; }
    int r = libusb_open(dev, &L.h);
    if (r != 0) {
        rigLog("open accessory failed: %s%s", libusb_error_name(r),
             r == LIBUSB_ERROR_ACCESS ? " (permissions: see README.md, udev rule for 18d1:2d0x)" : "");
        return false;
    }
    libusb_set_auto_detach_kernel_driver(L.h, 1);
    r = libusb_claim_interface(L.h, L.iface);
    if (r != 0) { rigLog("claim interface %d failed: %s", L.iface, libusb_error_name(r)); return false; }
    rigLog("accessory interface %d: bulk in 0x%02x, out 0x%02x (max packet %d)", L.iface, L.ep_in, L.ep_out,
         L.max_packet_out);
    return true;
}

// USB -> session: whatever arrives, as a byte stream.
void* rxThread(void* arg) {
    Link& L = *static_cast<Link*>(arg);
    std::vector<unsigned char> buf(kRxBuf);
    while (!L.done) {
        int n = 0;
        int r = libusb_bulk_transfer(L.h, L.ep_in, buf.data(), (int)buf.size(), &n, 500);
        if (r == LIBUSB_ERROR_TIMEOUT && n == 0) continue;
        if (r != 0 && r != LIBUSB_ERROR_TIMEOUT) {
            if (!L.done) rigLog("bulk IN ended: %s", libusb_error_name(r));
            break;
        }
        for (int off = 0; off < n;) {
            ssize_t w = send(L.sock, buf.data() + off, n - off, MSG_NOSIGNAL);
            if (w <= 0) { L.done = true; break; }
            off += (int)w;
        }
        L.rx_bytes += n;
    }
    L.done = true;
    shutdown(L.sock, SHUT_RDWR);
    return NULL;
}

bool usbWrite(Link& L, const uint8_t* p, size_t n) {
    while (n > 0) {
        size_t c = n < kTxBuf ? n : kTxBuf;
        int done = 0;
        int r = libusb_bulk_transfer(L.h, L.ep_out, const_cast<uint8_t*>(p), (int)c, &done, 5000);
        if (r != 0 || done != (int)c) {
            rigLog("bulk OUT failed: %s (%d of %zu)", libusb_error_name(r), done, c);
            return false;
        }
        p += c;
        n -= c;
        if (n == 0 && c % L.max_packet_out == 0 && L.zlp_ok) {
            r = libusb_bulk_transfer(L.h, L.ep_out, const_cast<uint8_t*>(p), 0, &done, 1000);
            if (r != 0) { L.zlp_ok = false; rigLog("zero-length USB write refused (%s): not sending them any more",
                                                 libusb_error_name(r)); }
        }
    }
    return true;
}

// Session -> USB: one whole AA frame per bulk transfer (frameLength), as the car path does.
void* txThread(void* arg) {
    Link& L = *static_cast<Link*>(arg);
    std::vector<uint8_t> acc;
    std::vector<uint8_t> buf(kTxBuf);
    while (!L.done) {
        ssize_t r = recv(L.sock, buf.data(), buf.size(), 0);
        if (r <= 0) break;
        acc.insert(acc.end(), buf.data(), buf.data() + r);
        size_t off = 0;
        for (;;) {
            size_t len = nbt::aoap::frameLength(acc.data() + off, acc.size() - off);
            if (len == 0 || acc.size() - off < len) break;
            if (!usbWrite(L, acc.data() + off, len)) { L.done = true; break; }
            L.tx_bytes += len;
            ++L.tx_frames;
            off += len;
        }
        acc.erase(acc.begin(), acc.begin() + off);
    }
    L.done = true;
    return NULL;
}

struct Recorder {
    FILE* video = NULL;
    uint64_t video_frames = 0, video_bytes = 0, codec_configs = 0;
    uint64_t audio_frames[32] = {0};
    int focus_requests = 0;
    volatile bool ended = false;
};

void onVideo(uint32_t, const uint8_t* nal, size_t len, uint64_t, void* u) {
    Recorder& R = *static_cast<Recorder*>(u);
    if (R.video) fwrite(nal, 1, len, R.video);
    ++R.video_frames;
    R.video_bytes += len;
}
void onCodec(const uint8_t* cfg, size_t len, void* u) {
    Recorder& R = *static_cast<Recorder*>(u);
    if (R.video) fwrite(cfg, 1, len, R.video);
    ++R.codec_configs;
    rigLog("codec config, %zu bytes", len);
}
void onAudio(uint8_t ch, const uint8_t*, size_t, uint64_t, void* u) {
    Recorder& R = *static_cast<Recorder*>(u);
    ++R.audio_frames[ch & 31];
}
void onAudioState(uint8_t ch, bool started, void*) { rigLog("audio channel %u %s", ch, started ? "started" : "stopped"); }
void onFocus(bool projected, uint32_t reason, void* u) {
    ++static_cast<Recorder*>(u)->focus_requests;
    rigLog("phone video focus request: %s (reason %u)", projected ? "projected" : "native", reason);
}
void onMic(bool open, void*) { rigLog("phone %s the microphone", open ? "opened" : "closed"); }
void onEnded(void* u) { static_cast<Recorder*>(u)->ended = true; rigLog("session ended"); }

void usage() {
    printf("usage: desk_rig [--seconds N] [--protocol 1|2] [--aoa-audio] [--manufacturer S] [--model S]\n"
           "                [--device VID:PID] [--out DIR] [--list]\n"
           "  --seconds N      stop after N s of session (default 60; Ctrl-C stops earlier)\n"
           "  --protocol P     identity.protocol as the daemon sends it (default 1; patched builds use 2)\n"
           "  --aoa-audio      AOA2 SET_AUDIO_MODE before START\n"
           "  --manufacturer / --model   accessory identity (default: the daemon's, 'Android' / 'Android Auto')\n"
           "  --device VID:PID which phone, when more than one is plugged in\n"
           "  --out DIR        where video.h264 goes (default build/out)\n"
           "  --list           only list phones / accessories and exit\n"
           "NBT_* settings apply as on the car (NBT_SSL_CIPHERS, NBT_VERBOSE, ...).\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : NULL;
        if (a == "--seconds" && v) { o.seconds = atoi(v); ++i; }
        else if (a == "--protocol" && v) { o.protocol = atoi(v); ++i; }
        else if (a == "--aoa-audio") o.audio_mode = true;
        else if (a == "--manufacturer" && v) { o.manufacturer = v; ++i; }
        else if (a == "--model" && v) { o.model = v; ++i; }
        else if (a == "--device" && v) {
            unsigned vid, pid;
            if (sscanf(v, "%x:%x", &vid, &pid) != 2) return false;
            o.vid = (int)vid; o.pid = (int)pid; ++i;
        }
        else if (a == "--out" && v) { o.out = v; ++i; }
        else if (a == "--list") o.list_only = true;
        else return false;
    }
    return o.seconds > 0;
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    Options o;
    if (!parse(argc, argv, o)) { usage(); return 2; }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    libusb_context* ctx = NULL;
    if (libusb_init(&ctx) != 0) { rigLog("libusb_init failed"); return 1; }

    if (o.list_only) {
        libusb_device* a = findDevice(ctx, o, true, true);
        libusb_device* p = findDevice(ctx, o, false, true);
        if (!a && !p) rigLog("no phone or accessory seen");
        if (a) libusb_unref_device(a);
        if (p) libusb_unref_device(p);
        libusb_exit(ctx);
        return 0;
    }

    libusb_device* acc = findDevice(ctx, o, true, false);
    if (acc) rigLog("phone is already in accessory mode: not switching again");
    else {
        if (!switchToAccessory(ctx, o)) { libusb_exit(ctx); return 1; }
        double t0 = now();
        while (!acc && now() - t0 < 15 && !g_stop) {
            usleep(250000);
            acc = findDevice(ctx, o, true, false);
        }
        if (!acc) {
            rigLog("phone did not come back as 18d1:2d0x within 15 s (it refused accessory mode? check the phone's"
                 " screen / logcat: CAR.SETUP, UsbAccessory)");
            libusb_exit(ctx);
            return 1;
        }
    }
    libusb_device_descriptor d;
    libusb_get_device_descriptor(acc, &d);
    rigLog("accessory %04x:%04x enumerated", d.idVendor, d.idProduct);

    Link L;
    bool ok = openAccessory(acc, L);
    libusb_unref_device(acc);
    if (!ok) { if (L.h) libusb_close(L.h); libusb_exit(ctx); return 1; }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { rigLog("socketpair: %s", strerror(errno)); return 1; }
    L.sock = sv[1];

    mkdir(o.out.c_str(), 0755);
    Recorder R;
    std::string vpath = o.out + "/video.h264";
    R.video = fopen(vpath.c_str(), "wb");

    pthread_t rx, tx;
    pthread_create(&rx, NULL, rxThread, &L);
    pthread_create(&tx, NULL, txThread, &L);
    rigLog("open: starting Android Auto over USB (desk rig), %d s", o.seconds);

    nbt::AASession s;
    s.setVideoFrameCallback(onVideo, &R);
    s.setCodecConfigCallback(onCodec, &R);
    s.setAudioFrameCallback(onAudio, &R);
    s.setAudioStateCallback(onAudioState, &R);
    s.setVideoFocusCallback(onFocus, &R);
    s.setMicRequestCallback(onMic, &R);
    s.setDisconnectCallback(onEnded, &R);
    if (!s.start(sv[0])) { rigLog("AASession::start failed"); L.done = true; }

    double t0 = now();
    while (!g_stop && !R.ended && !L.done && now() - t0 < o.seconds) usleep(200000);
    rigLog("stopping (%s)", g_stop ? "Ctrl-C" : R.ended ? "session ended" : L.done ? "USB link ended" : "time up");
    s.stop();
    L.done = true;
    shutdown(L.sock, SHUT_RDWR);
    pthread_join(rx, NULL);
    pthread_join(tx, NULL);
    close(L.sock);
    libusb_release_interface(L.h, L.iface);
    libusb_close(L.h);
    libusb_exit(ctx);
    if (R.video) fclose(R.video);

    rigLog("summary: %.0f s; USB rx %llu bytes, tx %llu bytes in %llu frames", now() - t0,
         (unsigned long long)L.rx_bytes, (unsigned long long)L.tx_bytes, (unsigned long long)L.tx_frames);
    rigLog("summary: video %llu frames, %llu bytes, %llu codec configs -> %s", (unsigned long long)R.video_frames,
         (unsigned long long)R.video_bytes, (unsigned long long)R.codec_configs, vpath.c_str());
    for (int ch = 0; ch < 32; ++ch)
        if (R.audio_frames[ch]) rigLog("summary: audio ch %d: %llu frames", ch, (unsigned long long)R.audio_frames[ch]);
    rigLog("summary: %d video focus requests from the phone", R.focus_requests);
    if (R.video_frames) rigLog("play it: ffplay -f h264 %s", vpath.c_str());
    return R.video_frames ? 0 : 3;
}
