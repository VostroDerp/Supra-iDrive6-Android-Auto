/* knockd — cryptographically armed SSH gate for the NBT Evo HU (QNX ARMv7LE).
 *
 * Listens on a TCP port. On connect: sends a fresh 32-byte challenge nonce;
 * the client answers with an RSA-2048 PKCS#1 v1.5 signature over SHA256 of the
 * challenge message. Verified against a PEM public key on the HU. A valid knock
 * opens sshd for OPEN_S seconds, then knockd SIGTERMs the listener again.
 *
 * Design goals: additive only (the stock FDAC unlock and the stock dropbear on
 * :22 are untouched), fail-safe (missing key/conf => exits, no SSH), tiny
 * (sequential accept, no threads), bounded (overall deadline per connection,
 * capped log, time-boxed SSH window).
 *
 * v2 (IMPROVEMENT-PLAN §A2/§A3/§C) adds:
 *   - an allowlist of local networks: the local address of every accepted socket
 *     must be inside ALLOW_NETS, else the connection is dropped with no nonce.
 *     knockd binds INADDR_ANY on purpose (addresses appear late at boot), so the
 *     check is per connection, not at bind time;
 *   - a time-boxed window instead of "sshd up until reboot";
 *   - sshd started as a tracked child (no system()), with one ListenAddress per
 *     allowed local address written into a generated copy of its config;
 *   - a control socket on UDP 127.0.0.1 so an iDrive menu toggle can open the
 *     same window without a knock (being in the car is the authorization);
 *   - protocol v2: the signature covers "nbt-knock-v2" || hu-id || nonce, so a
 *     knock signature is bound to this protocol and this head unit. v1
 *     (signature over the bare nonce) stays accepted while ACCEPT_V1=1, so an
 *     old knock.py keeps working;
 *   - a capped log and rate-limited "bad knock" lines, so a scanner cannot fill
 *     /var;
 *   - no randomness => no service (the old weak srand() fallback is gone).
 *
 * Config (/var/opt/aa/knock.conf), KEY=VALUE lines:
 *   PORT=851            listen port
 *   PUBKEY=<path>       PEM "RSA PUBLIC KEY" (PKCS#1) or hex modulus — REQUIRED
 *   SSHCMD=<cmd>        command run on a valid knock — REQUIRED. Split on spaces
 *                       (no quoting). If it contains "-f <path>", that file is
 *                       the sshd config template: knockd writes a copy with the
 *                       ListenAddress lines for the allowed local addresses and
 *                       passes the copy instead.
 *   SSHD_CONF_OUT=<p>   where that copy goes (default: template + ".active")
 *   LOG=<path>          append log, capped at LOG_MAX (default /var/opt/aa/knock.log)
 *   LOG_MAX=65536       bytes; the log is truncated with a marker line when over
 *   ALLOW_NETS=a/b,...  local networks allowed to knock (default: ENET link-local
 *                       + the car AP). Empty = allow any local address (v1
 *                       behaviour, not recommended).
 *   OPEN_S=900          how long sshd stays reachable after a knock
 *   CONN_S=8            overall deadline for one knock connection
 *   CTL_PORT=52791      UDP 127.0.0.1 control port ("open [s]", "close",
 *                       "status"); 0 disables it
 *   HU_ID=<string>      protocol v2 identity. Any short string that is NOT
 *                       derived from the VIN; both sides must agree.
 *   ACCEPT_V1=1         still accept v1 signatures (set 0 once knock.py is v2)
 *   PFCMD=<cmd>         optional command run once at start (e.g. pfctl -a ... -f ...)
 *                       to load a pf anchor so the car AP can reach these ports.
 *
 * Wire protocol:
 *   v1  S->C: 0x01 | nonce[32]                       sig over SHA256(nonce)
 *   v2  S->C: 0x02 | nonce[32] | idlen | hu_id[idlen]
 *             sig over SHA256("nbt-knock-v2" || hu_id || nonce)
 *   C->S: sig[RSA_size]                              (256 bytes for RSA-2048)
 *   S->C: 'K' window open, 'N' rejected, 'E' signature good but sshd failed.
 * A v1 client reads the first 33 bytes and ignores the rest, so the v2 hello is
 * backwards compatible on the wire.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/bio.h>

#define PROTO_V1    0x01
#define PROTO_V2    0x02
#define V2_CONTEXT  "nbt-knock-v2"
#define NONCE_LEN   32
#define MAX_SIG     4096
#define MAX_NETS    8
#define MAX_ADDRS   8
#define BAD_WINDOW  60          /* one "bad knock" summary line per minute */

static char conf_port[8]      = "851";
static char conf_pub[256]     = "/var/opt/aa/knock.pub";
static char conf_cmd[512]     = "";
static char conf_log[256]     = "/var/opt/aa/knock.log";
static char conf_nets[256]    = "169.254.0.0/16,172.16.224.0/24";
static char conf_huid[64]     = "nbt-id5";
static char conf_pfcmd[512]   = "";
static char conf_gen[256]     = "";
static long conf_log_max      = 65536;
static long conf_open_s       = 900;
static long conf_conn_s       = 8;
static int  conf_ctl_port     = 52791;
static int  conf_accept_v1    = 1;

/* ---- log (capped) ---------------------------------------------------------
 * C2: every bad knock used to append a line with no cap, so anyone who can
 * reach the port could fill /var and wear the flash. The log is truncated in
 * place (not renamed: it may live on a filesystem without rename()).
 */
static void klog(const char *fmt, ...) {
    FILE *f;
    va_list ap;
    char ts[32];
    time_t t;
    struct stat st;
    long over = -1;

    if (conf_log_max > 0 && stat(conf_log, &st) == 0 && (long)st.st_size >= conf_log_max)
        over = (long)st.st_size;

    f = fopen(conf_log, over >= 0 ? "w" : "a");
    if (!f) return;
    t = time(NULL);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    if (over >= 0)
        fprintf(f, "[%s] log truncated at %ld bytes (LOG_MAX=%ld)\n", ts, over, conf_log_max);
    va_start(ap, fmt);
    fprintf(f, "[%s] ", ts);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
    fclose(f);
}

/* Bad knocks are counted and summarised once per BAD_WINDOW (C2). */
static long  bad_count = 0;
static time_t bad_first = 0;
static char  bad_last_ip[INET_ADDRSTRLEN] = "";
static char  bad_last_why[64] = "";

static void bad_flush(void) {
    if (!bad_count) return;
    klog("%ld bad knock(s) in %ld s, last from %s (%s)", bad_count,
         (long)(time(NULL) - bad_first), bad_last_ip, bad_last_why);
    bad_count = 0;
    bad_first = 0;
}

static void bad_note(const char *ip, const char *why) {
    time_t now = time(NULL);
    if (!bad_count) bad_first = now;
    bad_count++;
    snprintf(bad_last_ip, sizeof bad_last_ip, "%s", ip);
    snprintf(bad_last_why, sizeof bad_last_why, "%s", why);
    if (now - bad_first >= BAD_WINDOW) bad_flush();
}

/* ---- config --------------------------------------------------------------- */
static void conf_set_long(const char *v, long *dst, long min, long max) {
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end && end != v && n >= min && n <= max) *dst = n;
}

static void load_conf(const char *path) {
    FILE *f = fopen(path, "r");
    char line[640];
    if (!f) return;                       /* defaults; SSHCMD empty => exits later */
    while (fgets(line, sizeof line, f)) {
        char *p = line, *eq, *v;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(p, "PORT"))          snprintf(conf_port, sizeof conf_port, "%s", v);
        else if (!strcmp(p, "PUBKEY"))   snprintf(conf_pub,  sizeof conf_pub,  "%s", v);
        else if (!strcmp(p, "SSHCMD"))   snprintf(conf_cmd,  sizeof conf_cmd,  "%s", v);
        else if (!strcmp(p, "LOG"))      snprintf(conf_log,  sizeof conf_log,  "%s", v);
        else if (!strcmp(p, "ALLOW_NETS")) snprintf(conf_nets, sizeof conf_nets, "%s", v);
        else if (!strcmp(p, "HU_ID"))    snprintf(conf_huid, sizeof conf_huid, "%s", v);
        else if (!strcmp(p, "PFCMD"))    snprintf(conf_pfcmd, sizeof conf_pfcmd, "%s", v);
        else if (!strcmp(p, "SSHD_CONF_OUT")) snprintf(conf_gen, sizeof conf_gen, "%s", v);
        else if (!strcmp(p, "LOG_MAX"))  conf_set_long(v, &conf_log_max, 0, 100L * 1024 * 1024);
        else if (!strcmp(p, "OPEN_S"))   conf_set_long(v, &conf_open_s, 10, 86400);
        else if (!strcmp(p, "CONN_S"))   conf_set_long(v, &conf_conn_s, 1, 120);
        else if (!strcmp(p, "CTL_PORT")) { long n = conf_ctl_port; conf_set_long(v, &n, 0, 65535); conf_ctl_port = (int)n; }
        else if (!strcmp(p, "ACCEPT_V1")) { long n = conf_accept_v1; conf_set_long(v, &n, 0, 1); conf_accept_v1 = (int)n; }
    }
    fclose(f);
}

/* ---- allowlist (A2) -------------------------------------------------------
 * ALLOW_NETS is a comma-separated list of a.b.c.d/len. The local address of an
 * accepted socket (getsockname), not the peer address, is what is checked: it
 * says which of the HU's networks the knock arrived on. The BM3 dongle's Wi-Fi
 * is bridged onto the same interface as ENET, so only the link-local range is
 * allowed there and the dongle's DHCP range is not.
 */
static struct { in_addr_t net, mask; } nets[MAX_NETS];
static int net_count = 0;

static int parse_nets(const char *spec) {
    char buf[256];
    char *p;
    net_count = 0;
    snprintf(buf, sizeof buf, "%s", spec);
    for (p = strtok(buf, ","); p && net_count < MAX_NETS; p = strtok(NULL, ",")) {
        char *slash;
        int len = 32;
        struct in_addr a;
        while (*p == ' ' || *p == '\t') p++;
        slash = strchr(p, '/');
        if (slash) {
            *slash = 0;
            len = atoi(slash + 1);
        }
        if (len < 0 || len > 32) return -1;
        if (!inet_aton(p, &a)) return -1;
        nets[net_count].mask = len ? htonl(0xffffffffu << (32 - len)) : 0;
        nets[net_count].net  = a.s_addr & nets[net_count].mask;
        net_count++;
    }
    return net_count;
}

/* No ALLOW_NETS configured => allow anything (the v1 behaviour). */
static int addr_allowed(in_addr_t a) {
    int i;
    if (!net_count) return 1;
    for (i = 0; i < net_count; i++)
        if ((a & nets[i].mask) == nets[i].net) return 1;
    return 0;
}

/* IPv4 addresses of up interfaces that are inside the allowlist. Used to write
 * sshd's ListenAddress lines; knockd starts sshd on demand, so by then the
 * addresses exist. */
static int local_allowed_addrs(in_addr_t *out, int max) {
    char buf[4096];
    struct ifconf ifc;
    char *p;
    int s, n = 0;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    memset(&ifc, 0, sizeof ifc);
    ifc.ifc_len = sizeof buf;
    ifc.ifc_buf = buf;
    if (ioctl(s, SIOCGIFCONF, &ifc) < 0) { close(s); return 0; }

    for (p = buf; p + sizeof(struct ifreq) <= buf + (size_t)ifc.ifc_len && n < max; ) {
        struct ifreq *ifr = (struct ifreq *)p;
        struct ifreq fl;
        in_addr_t a;
        int i, dup = 0;
#ifdef __linux__
        p += sizeof(struct ifreq);
#else
        p += sizeof(ifr->ifr_name) +
             ((size_t)ifr->ifr_addr.sa_len > sizeof(struct sockaddr)
                  ? (size_t)ifr->ifr_addr.sa_len : sizeof(struct sockaddr));
#endif
        if (ifr->ifr_addr.sa_family != AF_INET) continue;
        a = ((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr;
        if (!addr_allowed(a)) continue;
        memset(&fl, 0, sizeof fl);
        memcpy(fl.ifr_name, ifr->ifr_name, sizeof fl.ifr_name);
        if (ioctl(s, SIOCGIFFLAGS, &fl) == 0 && !(fl.ifr_flags & IFF_UP)) continue;
        for (i = 0; i < n; i++) if (out[i] == a) dup = 1;
        if (!dup) out[n++] = a;
    }
    close(s);
    return n;
}

/* ---- public key ----------------------------------------------------------- */
static RSA *load_pubkey(const char *path) {
    FILE *f = fopen(path, "r");
    char buf[8192];
    size_t len;
    RSA *rsa = NULL;
    char *nl, *p;
    BIGNUM *n = NULL;
    unsigned long e = 65537;

    if (!f) { klog("fopen %s failed: %s", path, strerror(errno)); return NULL; }
    len = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[len] = 0;

    if (buf[0] == '-') {                              /* PEM block */
        BIO *bio = BIO_new_mem_buf(buf, (int)len);
        rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
        if (!rsa) {
            BIO_free(bio);
            bio = BIO_new_mem_buf(buf, (int)len);
            {
                EVP_PKEY *pk = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
                if (pk) { rsa = EVP_PKEY_get1_RSA(pk); EVP_PKEY_free(pk); }
            }
        }
        BIO_free(bio);
        if (!rsa)
            klog("PEM parse %s failed: %s", path, ERR_error_string(ERR_get_error(), NULL));
        return rsa;
    }
    /* raw hex modulus (line 1; optional "Modulus=" prefix),
       optional "e=<hex>" on line 2 — default 65537 */
    nl = strchr(buf, '\n');
    if (nl) *nl = 0;
    p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (!strncasecmp(p, "modulus=", 8)) p += 8;
    if (nl && !strncasecmp(nl + 1, "e=", 2)) e = strtoul(nl + 3, NULL, 16);
    if (BN_hex2bn(&n, p) > 0 && n) {
        BIGNUM *be = BN_new();
        BN_set_word(be, e);
        rsa = RSA_new();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        RSA_set0_key(rsa, n, be, NULL);        /* host builds (OpenSSL >= 1.1) */
#else
        rsa->n = n;                            /* the car's OpenSSL 1.0.x */
        rsa->e = be;
#endif
    } else {
        klog("hex modulus parse %s failed", path);
    }
    return rsa;
}

/* ---- randomness (C3: fail closed) ---------------------------------------- */
static int rand32(unsigned char *out) {
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got = 0;
    if (fd < 0) fd = open("/dev/random", O_RDONLY);
    if (fd < 0) return -1;
    while (got < NONCE_LEN) {
        ssize_t r = read(fd, out + got, NONCE_LEN - got);
        if (r <= 0) break;
        got += r;
    }
    close(fd);
    return got == NONCE_LEN ? 0 : -1;
}

/* ---- monotonic seconds ---------------------------------------------------- */
static long mono(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (long)ts.tv_sec;
#endif
    return (long)time(NULL);
}

/* ---- child processes (C4: no system(), the sshd pid is tracked) ----------- */
static int tokenize(const char *cmd, char *store, size_t store_len, char **argv, int max) {
    int n = 0;
    char *p;
    snprintf(store, store_len, "%s", cmd);
    for (p = strtok(store, " \t"); p && n < max - 1; p = strtok(NULL, " \t"))
        argv[n++] = p;
    argv[n] = NULL;
    return n;
}

static pid_t spawn_argv(char **argv) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }
        execv(argv[0], argv);
        _exit(127);
    }
    return pid;
}

/* Wait up to tries*50 ms for a child to exit. Returns its exit status, or -2
 * while it is still running, or -1 if it cannot be waited for. */
#define CHILD_RUNNING (-2)
static int wait_child(pid_t pid, int tries) {
    int i;
    for (i = 0; i < tries; i++) {
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        if (r < 0) return -1;
        usleep(50000);
    }
    return CHILD_RUNNING;
}

static int ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && !strcmp(s + ls - lx, suffix);
}

/* ---- sshd config with one ListenAddress per allowed address (A2) ---------
 * The template is the SSHCMD "-f" file. Its own ListenAddress lines are dropped
 * and replaced. If the copy cannot be written, sshd is started on the template
 * as-is: a knock already had to come from an allowed network, and the window is
 * time-boxed, so that is the lockout-safe choice. It is logged loudly.
 */
static int write_sshd_conf(const char *base, const char *out, in_addr_t *addrs, int n) {
    FILE *in, *of;
    char line[512];
    int i;

    in = fopen(base, "r");
    if (!in) { klog("sshd config %s unreadable: %s", base, strerror(errno)); return -1; }
    of = fopen(out, "w");
    if (!of) { klog("cannot write %s: %s", out, strerror(errno)); fclose(in); return -1; }
    fprintf(of, "# generated by knockd from %s — do not edit\n", base);
    while (fgets(line, sizeof line, in)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncasecmp(p, "ListenAddress", 13)) continue;
        fputs(line, of);
    }
    fclose(in);
    for (i = 0; i < n; i++) {
        struct in_addr a;
        a.s_addr = addrs[i];
        fprintf(of, "ListenAddress %s\n", inet_ntoa(a));
    }
    if (fclose(of) != 0) { klog("cannot finish %s: %s", out, strerror(errno)); return -1; }
    return 0;
}

static pid_t sshd_pid = 0;
static long  window_until = 0;          /* mono() seconds; 0 = closed */

static int sshd_alive(void) {
    if (sshd_pid <= 0) return 0;
    if (kill(sshd_pid, 0) == 0) return 1;
    if (errno == EPERM) return 1;
    return 0;
}

static void reap_sshd(void) {
    int st = 0;
    if (sshd_pid <= 0) return;
    if (waitpid(sshd_pid, &st, WNOHANG) == sshd_pid) {
        klog("sshd pid %d exited (status %d)", (int)sshd_pid,
             WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        sshd_pid = 0;
    }
}

/* Start sshd unless it is already up. Returns 0 when the listener should be
 * reachable.
 *
 * The listener has to be OUR child, or the window (A3) has nothing to SIGTERM.
 * sshd daemonises by default, which would leave the listener as an untracked
 * grandchild, so "-D" (stay in the foreground) is added when SSHCMD names sshd
 * and does not have it already. Its log still goes to /dev/null; the knock log
 * is here. Established sessions are the listener's own children and survive its
 * SIGTERM, which is exactly what A3 wants.
 */
static int start_sshd(void) {
    static char store[512];                           /* argv points into this */
    static char genpath[256];
    char *argv[34];
    in_addr_t addrs[MAX_ADDRS];
    int argc, i, fidx = -1, n, rc;
    pid_t pid;

    reap_sshd();
    if (sshd_alive()) return 0;                       /* C4: never a second sshd */
    if (!conf_cmd[0]) { klog("no SSHCMD configured"); return -1; }

    argc = tokenize(conf_cmd, store, sizeof store, argv, 32);
    if (argc < 1) { klog("SSHCMD is empty"); return -1; }
    for (i = 0; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-f")) fidx = i + 1;

    if (fidx > 0) {
        n = local_allowed_addrs(addrs, MAX_ADDRS);
        if (n == 0) {
            /* Nothing of ours is up yet: starting sshd on 0.0.0.0 would hand the
             * window to the networks the allowlist exists to keep out. */
            klog("no allowed local address is up (ALLOW_NETS=%s) — not starting sshd", conf_nets);
            return -1;
        }
        if (conf_gen[0]) snprintf(genpath, sizeof genpath, "%s", conf_gen);
        else snprintf(genpath, sizeof genpath, "%s.active", argv[fidx]);
        if (write_sshd_conf(argv[fidx], genpath, addrs, n) == 0) {
            klog("sshd config %s: %d ListenAddress line(s) from ALLOW_NETS", genpath, n);
            argv[fidx] = genpath;
        } else {
            klog("WARNING: using %s unchanged — sshd keeps its own ListenAddress "
                 "(the allowlist then only guards knockd)", argv[fidx]);
        }
    }

    {
        int have_D = 0;
        for (i = 0; i < argc; i++) if (!strcmp(argv[i], "-D")) have_D = 1;
        if (!have_D && ends_with(argv[0], "sshd") && argc + 1 < (int)(sizeof argv / sizeof argv[0]) - 1) {
            argv[argc++] = (char *)"-D";
            argv[argc] = NULL;
        }
    }

    pid = spawn_argv(argv);
    if (pid < 0) { klog("fork for sshd failed: %s", strerror(errno)); return -1; }

    /* Give it half a second to fail on its own (bad config, port already bound,
     * missing host key). Still running = the listener is up. */
    rc = wait_child(pid, 10);
    if (rc != CHILD_RUNNING) {
        if (rc > 0) {
            klog("sshd exited with status %d (config error? port already bound? host key?)", rc);
            return -1;
        }
        /* Exited 0: it daemonised anyway (no -D support, or dropbear). The
         * listener is then not our child and the window cannot close it. */
        sshd_pid = 0;
        klog("WARNING: '%s' detached (exit 0): the listener is not our child, so the "
             "window cannot stop it — add -D (sshd) / -F (dropbear) to SSHCMD", conf_cmd);
        return 0;
    }
    sshd_pid = pid;
    klog("sshd listener started, pid %d (%s)", (int)sshd_pid, conf_cmd);
    return 0;
}

static void open_window(long secs, const char *why) {
    long now = mono();
    int was_open = window_until > now;
    if (secs <= 0) secs = conf_open_s;
    if (start_sshd() != 0) { window_until = 0; return; }
    window_until = now + secs;
    klog("SSH window %s for %ld s (%s)", was_open ? "extended" : "open", secs, why);
}

/* Close the window: SIGTERM the listener. Established sessions are separate
 * processes and keep running (A3) — only new connections are refused. */
static void close_window(const char *why) {
    window_until = 0;
    reap_sshd();
    if (sshd_alive()) {
        if (kill(sshd_pid, SIGTERM) == 0)
            klog("SSH window closed (%s): SIGTERM to sshd pid %d; open sessions keep running", why, (int)sshd_pid);
        else
            klog("SSH window closed (%s): cannot signal sshd pid %d: %s", why, (int)sshd_pid, strerror(errno));
    } else {
        klog("SSH window closed (%s): no tracked sshd listener", why);
    }
    sshd_pid = 0;
}

/* ---- one knock connection (C1: one overall deadline) --------------------- */
static int recv_until(int fd, unsigned char *buf, int want, long deadline) {
    int got = 0;
    while (got < want) {
        struct timeval tv;
        fd_set rfds;
        long left = deadline - mono();
        int r;
        if (left <= 0) return got;
        tv.tv_sec = left;
        tv.tv_usec = 0;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) { if (errno == EINTR) continue; return got; }
        if (r == 0) return got;                       /* deadline */
        r = (int)recv(fd, buf + got, want - got, 0);
        if (r <= 0) return got;
        got += r;
    }
    return got;
}

static int verify_sig(RSA *rsa, const unsigned char *nonce, const unsigned char *sig, int siglen) {
    unsigned char h[SHA256_DIGEST_LENGTH];
    unsigned char msg[sizeof(V2_CONTEXT) + sizeof conf_huid + NONCE_LEN];
    size_t ctx_len = strlen(V2_CONTEXT), id_len = strlen(conf_huid), off = 0;

    memcpy(msg, V2_CONTEXT, ctx_len);           off += ctx_len;
    memcpy(msg + off, conf_huid, id_len);       off += id_len;
    memcpy(msg + off, nonce, NONCE_LEN);        off += NONCE_LEN;
    SHA256(msg, off, h);
    if (RSA_verify(NID_sha256, h, SHA256_DIGEST_LENGTH, (unsigned char *)sig, siglen, rsa) == 1)
        return 2;                                /* v2 */
    if (conf_accept_v1) {
        SHA256(nonce, NONCE_LEN, h);
        if (RSA_verify(NID_sha256, h, SHA256_DIGEST_LENGTH, (unsigned char *)sig, siglen, rsa) == 1)
            return 1;                            /* v1 */
    }
    return 0;
}

static void handle(int c, RSA *rsa, struct sockaddr_in *peer) {
    struct timeval tv;
    unsigned char nonce[NONCE_LEN];
    unsigned char hello[1 + NONCE_LEN + 1 + sizeof conf_huid];
    unsigned char sig[MAX_SIG];
    char ip[INET_ADDRSTRLEN] = "?";
    struct sockaddr_in local;
    socklen_t ll = sizeof local;
    long deadline = mono() + conf_conn_s;
    size_t hl;
    size_t id_len = strlen(conf_huid);
    int one = 1, got, ver, siglen;

    inet_ntop(AF_INET, &peer->sin_addr, ip, sizeof ip);

    /* A2: which of our networks did this arrive on? */
    memset(&local, 0, sizeof local);
    if (getsockname(c, (struct sockaddr *)&local, &ll) != 0 || !addr_allowed(local.sin_addr.s_addr)) {
        char lip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &local.sin_addr, lip, sizeof lip);
        bad_note(ip, "off-allowlist local address");
        klog("dropped %s -> %s: not in ALLOW_NETS=%s", ip, lip, conf_nets);
        return;
    }

    /* C6: a timeout on send() too, so a client that never reads cannot hold us. */
    tv.tv_sec = conf_conn_s; tv.tv_usec = 0;
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    /* C3: no randomness, no service. */
    if (rand32(nonce) != 0) {
        klog("no randomness (/dev/urandom unreadable) — refusing %s", ip);
        return;
    }

    if (id_len > 255) id_len = 255;
    hello[0] = PROTO_V2;
    memcpy(hello + 1, nonce, NONCE_LEN);
    hello[1 + NONCE_LEN] = (unsigned char)id_len;
    memcpy(hello + 2 + NONCE_LEN, conf_huid, id_len);
    hl = 2 + NONCE_LEN + id_len;
    if (send(c, hello, hl, 0) != (ssize_t)hl) return;

    siglen = (int)RSA_size(rsa);
    if (siglen > MAX_SIG) return;
    got = recv_until(c, sig, siglen, deadline);
    if (got != siglen) {
        char why[64];
        snprintf(why, sizeof why, "%d of %d signature bytes", got, siglen);
        bad_note(ip, why);
        send(c, "N", 1, 0);
        return;
    }

    ver = verify_sig(rsa, nonce, sig, siglen);
    if (!ver) {
        bad_note(ip, "signature did not verify");
        send(c, "N", 1, 0);
        return;
    }

    bad_flush();
    klog("valid knock (proto v%d) from %s", ver, ip);
    open_window(conf_open_s, "knock");
    send(c, window_until ? "K" : "E", 1, 0);
}

/* ---- control socket (A5): the iDrive toggle opens the window with no knock -
 * Bound to 127.0.0.1 only, so nothing off the box can reach it. Commands:
 *   "open" / "open <secs>"   open or extend the window
 *   "close"                  close it now
 *   "status"                 reply "open <secs left> <sshd pid>" / "closed"
 */
static int ctl_open(void) {
    struct sockaddr_in a;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)conf_ctl_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) {
        klog("control port 127.0.0.1:%d unavailable: %s", conf_ctl_port, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

static void ctl_handle(int s) {
    char buf[128];
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
    char reply[96];
    if (n <= 0) return;
    buf[n] = 0;
    buf[strcspn(buf, "\r\n")] = 0;
    if (from.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return;

    if (!strncmp(buf, "open", 4)) {
        long secs = atol(buf + 4);                    /* "" or junk => 0 => OPEN_S */
        if (secs < 0 || secs > 86400) secs = 0;
        open_window(secs, "iDrive toggle");
        if (window_until) snprintf(reply, sizeof reply, "open %ld\n", window_until - mono());
        else snprintf(reply, sizeof reply, "failed\n");
    } else if (!strcmp(buf, "close")) {
        if (window_until) close_window("iDrive toggle");
        snprintf(reply, sizeof reply, "closed\n");
    } else if (!strcmp(buf, "status")) {
        long left = window_until ? window_until - mono() : 0;
        if (left < 0) left = 0;
        if (window_until) snprintf(reply, sizeof reply, "open %ld %d\n", left, (int)sshd_pid);
        else snprintf(reply, sizeof reply, "closed\n");
    } else {
        snprintf(reply, sizeof reply, "?\n");
    }
    sendto(s, reply, strlen(reply), 0, (struct sockaddr *)&from, fl);
}

#ifndef KNOCKD_NO_MAIN
int main(int argc, char **argv) {
    const char *conf = (argc > 1) ? argv[1] : "/var/opt/aa/knock.conf";
    unsigned char probe[NONCE_LEN];
    struct sigaction sa;
    struct sockaddr_in a;
    RSA *rsa;
    int s, ctl = -1, one = 1;

    load_conf(conf);
    if (!conf_cmd[0]) {
        klog("no SSHCMD configured — exiting (fail-safe: nothing to arm)");
        return 1;
    }
    if (parse_nets(conf_nets) < 0) {
        klog("ALLOW_NETS=%s is not a list of a.b.c.d/len — exiting (fail-safe)", conf_nets);
        return 1;
    }
    if (rand32(probe) != 0) {
        klog("no usable /dev/urandom — exiting (fail-safe: a guessable nonce is no gate)");
        return 1;
    }
    rsa = load_pubkey(conf_pub);
    if (!rsa) {
        klog("no usable public key at %s — exiting (fail-safe)", conf_pub);
        return 1;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)atoi(conf_port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);            /* per-connection check instead */
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 4) < 0) {
        klog("bind/listen :%s failed: %s", conf_port, strerror(errno));
        return 1;
    }
    if (conf_ctl_port) ctl = ctl_open();

    signal(SIGPIPE, SIG_IGN);   /* a client RST must not kill us on send */

    /* C4: children are reaped with waitpid, so SA_NOCLDWAIT is gone. */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);

    if (fork() > 0) return 0;             /* parent returns; child serves */
    setsid();
    {
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }
    }

    klog("knockd v2 listening on :%s (pub=%s hu=%s allow=%s open=%lds ctl=%d cmd=%.60s)",
         conf_port, conf_pub, conf_huid, conf_nets, conf_open_s, conf_ctl_port, conf_cmd);

    /* A2: optional pf anchor so the car's own AP can reach these ports. Not
     * persistent, so it is loaded here on every start. */
    if (conf_pfcmd[0]) {
        char store[512];
        char *pargv[32];
        if (tokenize(conf_pfcmd, store, sizeof store, pargv, 32) > 0) {
            pid_t pid = spawn_argv(pargv);
            int rc = pid < 0 ? -1 : wait_child(pid, 40);
            klog("PFCMD %s -> rc %d%s", conf_pfcmd, rc,
                 rc == CHILD_RUNNING ? " (still running)" : "");
        }
    }

    for (;;) {
        fd_set rfds;
        struct timeval tv;
        long now, left = 60;
        int maxfd = s, r;

        reap_sshd();
        if (bad_count && time(NULL) - bad_first >= BAD_WINDOW) bad_flush();
        now = mono();
        if (window_until && now >= window_until) close_window("window expired");
        if (window_until) {
            long w = window_until - now;
            if (w < left) left = w;
        }
        if (bad_count) {
            long b = BAD_WINDOW - (long)(time(NULL) - bad_first);
            if (b < 0) b = 0;
            if (b < left) left = b;
        }
        if (left < 1) left = 1;

        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        if (ctl >= 0) { FD_SET(ctl, &rfds); if (ctl > maxfd) maxfd = ctl; }
        tv.tv_sec = left;
        tv.tv_usec = 0;
        r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) { if (errno == EINTR) continue; sleep(1); continue; }
        if (r == 0) continue;                         /* timeout: loop re-checks the window */

        if (ctl >= 0 && FD_ISSET(ctl, &rfds)) ctl_handle(ctl);
        if (FD_ISSET(s, &rfds)) {
            struct sockaddr_in p;
            socklen_t pl = sizeof p;
            int c = accept(s, (struct sockaddr *)&p, &pl);
            if (c < 0) { if (errno != EINTR) sleep(1); continue; }
            handle(c, rsa, &p);
            close(c);
        }
    }
}
#endif /* KNOCKD_NO_MAIN */
