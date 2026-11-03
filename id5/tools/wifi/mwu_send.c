/* mwu_send: send one command to mwud (the head unit's Wi-Fi control daemon) and print its reply.
 *
 * Runs ON the head unit (QNX), over our SSH: `car.sh sh '/var/opt/aa/mwu-send cmd=ap_status iface=uap0'`.
 * It replaces the earlier workarounds (patched mwud_pub, gdb/pdebug injection), which were only needed because
 * the BM3 dongle dropped laptop->HU UDP and the HU has no UDP tool of its own.
 *
 * Wire format (findings.md §10, from mwud's disassembly; mwu.py is the laptop-side equivalent):
 *   datagram = u32 LE body_len | u32 seq (echoed back; 0 works) | body
 *   body     = "k=v\n" lines, every line \n-terminated, module=<name> required (default here: mwpamod)
 * Replies come back to the sender's address in the same format.
 *
 * usage: mwu-send [-p port] [-h host] [-t seconds] [module=<m>] k=v ...
 *   default host 127.0.0.1, port 9096 (mwud's command port), timeout 3 s, module=mwpamod.
 *   exit 0 = reply printed, 1 = no reply, 2 = usage / socket error.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_BODY 4096

static void put_u32le(unsigned char *p, unsigned v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
static unsigned get_u32le(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static int usage(void) {
    fprintf(stderr, "usage: mwu-send [-p port] [-h host] [-t seconds] [module=<m>] k=v ...\n"
                    "  e.g. mwu-send cmd=ap_init iface=uap0 ssid=TEST key=12345678 auth=0020 encrypt=0008\n");
    return 2;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int port = 9096, timeout_s = 3, i, have_module = 0;
    unsigned char pkt[8 + MAX_BODY], rep[8 + 8192];
    size_t blen = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) timeout_s = atoi(argv[++i]);
        else break;
    }
    if (i >= argc || port <= 0 || port > 65535 || timeout_s <= 0) return usage();

    for (; i < argc; ++i) {
        size_t n = strlen(argv[i]);
        if (!strchr(argv[i], '=') || strchr(argv[i], '\n')) {
            fprintf(stderr, "mwu-send: not a k=v argument: %s\n", argv[i]);
            return 2;
        }
        if (!strncmp(argv[i], "module=", 7)) have_module = 1;
        if (blen + n + 1 + 16 > MAX_BODY) { fprintf(stderr, "mwu-send: body too long\n"); return 2; }
        memcpy(pkt + 8 + blen, argv[i], n);
        blen += n;
        pkt[8 + blen++] = '\n';
    }
    if (!have_module) {                         /* module first is not required, only present */
        const char *m = "module=mwpamod\n";
        size_t n = strlen(m);
        memmove(pkt + 8 + n, pkt + 8, blen);
        memcpy(pkt + 8, m, n);
        blen += n;
    }
    put_u32le(pkt, (unsigned)blen);
    put_u32le(pkt + 4, 0);

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("mwu-send: socket"); return 2; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) { fprintf(stderr, "mwu-send: bad host %s\n", host); return 2; }
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { perror("mwu-send: connect"); return 2; }

    fwrite(">>> ", 1, 4, stdout);
    fwrite(pkt + 8, 1, blen, stdout);
    fflush(stdout);
    if (send(s, pkt, 8 + blen, 0) != (ssize_t)(8 + blen)) { perror("mwu-send: send"); return 2; }

    fd_set rf;
    struct timeval tv;
    FD_ZERO(&rf);
    FD_SET(s, &rf);
    tv.tv_sec = timeout_s;
    tv.tv_usec = 0;
    int r = select(s + 1, &rf, NULL, NULL, &tv);
    if (r <= 0) { printf("<<< (no reply in %d s%s)\n", timeout_s, r < 0 ? ", select error" : ""); return 1; }
    ssize_t n = recv(s, rep, sizeof rep, 0);
    if (n < 0) {
        /* ECONNREFUSED here means nothing listens on the port: mwud is not running. */
        printf("<<< (recv error: %s%s)\n", strerror(errno), errno == ECONNREFUSED ? ": is mwud running?" : "");
        return 1;
    }
    if (n < 8) { printf("<<< (short reply, %d bytes)\n", (int)n); return 1; }
    unsigned rl = get_u32le(rep);
    if (rl > (unsigned)(n - 8)) rl = (unsigned)(n - 8);
    printf("<<< ");
    fwrite(rep + 8, 1, rl, stdout);
    if (rl == 0 || rep[8 + rl - 1] != '\n') putchar('\n');
    close(s);
    return 0;
}
