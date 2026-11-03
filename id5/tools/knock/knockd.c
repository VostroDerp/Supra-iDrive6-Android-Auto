/* knockd — cryptographically armed SSH gate for the NBT Evo HU (QNX ARMv7LE).
 *
 * Listens on a TCP port. On connect: sends a fresh 32-byte challenge nonce;
 * client answers with an RSA-2048 PKCS#1 v1.5 signature over SHA256(nonce).
 * Verified against a PEM public key on the HU. On success it runs the SSH
 * bring-up command from the config file; on failure it closes and moves on.
 *
 * Design goals: additive only (stock unlock untouched), fail-safe (missing
 * key/conf => exits, no SSH), tiny (sequential accept, no threads), robust
 * (timeouts everywhere, SA_NOCLDWAIT, drops privileges? no — needs root to
 * spawn sshd; keep surface minimal instead).
 *
 * Config (/var/opt/aa/knock.conf), KEY=VALUE lines:
 *   PORT=46610          listen port
 *   PUBKEY=<path>       PEM "RSA PUBLIC KEY" (PKCS#1) — REQUIRED, else exit
 *   SSHCMD=<cmd>        command run via system() on a valid knock — REQUIRED
 *   LOG=<path>          append-only log (default /var/opt/aa/knock.log)
 *
 * Wire protocol:
 *   S->C: 0x01 | nonce[32]
 *   C->S: sig[RSA_size]            (256 bytes for RSA-2048)
 *   S->C: 'K' on success, 'N' on failure, then close.
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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/bio.h>

#define PROTO_VER   0x01
#define NONCE_LEN   32
#define MAX_SIG     4096
#define RECV_TMO_S  6

static char conf_port[8] = "46610";
static char conf_pub[256] = "/var/opt/aa/knock.pub";
static char conf_cmd[512] = "";
static char conf_log[256] = "/var/opt/aa/knock.log";

static void klog(const char *fmt, ...) {
    FILE *f = fopen(conf_log, "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    char ts[32]; time_t t = time(NULL);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "[%s] ", ts);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap); fclose(f);
}

static void load_conf(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;                       /* defaults; SSHCMD empty => exits later */
    char line[640];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(p, "PORT"))   snprintf(conf_port, sizeof conf_port, "%s", v);
        if (!strcmp(p, "PUBKEY")) snprintf(conf_pub,  sizeof conf_pub,  "%s", v);
        if (!strcmp(p, "SSHCMD")) snprintf(conf_cmd,  sizeof conf_cmd,  "%s", v);
        if (!strcmp(p, "LOG"))    snprintf(conf_log,  sizeof conf_log,  "%s", v);
    }
    fclose(f);
}

static RSA *load_pubkey(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { klog("fopen %s failed: %s", path, strerror(errno)); return NULL; }
    char buf[8192];
    size_t len = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[len] = 0;
    RSA *rsa = NULL;
    if (buf[0] == '-') {                              /* PEM block */
        BIO *bio = BIO_new_mem_buf(buf, (int)len);
        rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
        if (!rsa) {
            BIO_free(bio);
            bio = BIO_new_mem_buf(buf, (int)len);
            EVP_PKEY *pk = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
            if (pk) { rsa = EVP_PKEY_get1_RSA(pk); EVP_PKEY_free(pk); }
        }
        BIO_free(bio);
        if (!rsa)
            klog("PEM parse %s failed: %s", path,
                 ERR_error_string(ERR_get_error(), NULL));
        return rsa;
    }
    /* raw hex modulus (line 1; optional "Modulus=" prefix),
       optional "e=<hex>" on line 2 — default 65537 */
    char *nl = strchr(buf, '\n');
    if (nl) *nl = 0;
    char *p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (!strncasecmp(p, "modulus=", 8)) p += 8;
    BIGNUM *n = NULL;
    unsigned long e = 65537;
    if (nl) {
        char *ep = nl + 1;
        if (!strncasecmp(ep, "e=", 2)) e = strtoul(ep + 2, NULL, 16);
    }
    if (BN_hex2bn(&n, p) > 0 && n) {
        rsa = RSA_new();
        rsa->n = n;
        rsa->e = BN_new();
        BN_set_word(rsa->e, e);
        rsa->flags |= RSA_FLAG_NO_CONSTTIME;
    } else {
        klog("hex modulus parse %s failed", path);
    }
    return rsa;
}

static int rand32(unsigned char *out) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) fd = open("/dev/random", O_RDONLY);
    if (fd >= 0) {
        ssize_t got = 0;
        while (got < NONCE_LEN) {
            ssize_t r = read(fd, out + got, NONCE_LEN - got);
            if (r <= 0) break;
            got += r;
        }
        close(fd);
        if (got == NONCE_LEN) return 0;
    }
    /* fallback — weak but only a freshness nonce */
    srand((unsigned)(time(NULL) ^ getpid() ^ (unsigned)(size_t)out));
    for (int i = 0; i < NONCE_LEN; i++) out[i] = (unsigned char)rand();
    return 0;
}

static void handle(int c, RSA *rsa, struct sockaddr_in *peer) {
    struct timeval tv = { RECV_TMO_S, 0 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int one = 1;
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    unsigned char nonce[NONCE_LEN];
    rand32(nonce);
    unsigned char hello[1 + NONCE_LEN];
    hello[0] = PROTO_VER;
    memcpy(hello + 1, nonce, NONCE_LEN);
    if (send(c, hello, sizeof hello, 0) != (ssize_t)sizeof hello) return;

    unsigned char sig[MAX_SIG];
    int got = 0;
    while (got < (int)RSA_size(rsa) && got < MAX_SIG) {
        ssize_t r = recv(c, sig + got, sizeof sig - got, 0);
        if (r <= 0) break;
        got += (int)r;
    }

    unsigned char h[SHA256_DIGEST_LENGTH];
    SHA256(nonce, NONCE_LEN, h);
    int ok = (got == (int)RSA_size(rsa)) &&
             RSA_verify(NID_sha256, h, SHA256_DIGEST_LENGTH, sig, got, rsa) == 1;

    char ip[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &peer->sin_addr, ip, sizeof ip);
    if (ok) {
        klog("valid knock from %s — running SSHCMD", ip);
        send(c, "K", 1, 0);
        int rc = conf_cmd[0] ? system(conf_cmd) : -1;
        klog("SSHCMD rc=%d (%s)", rc, conf_cmd[0] ? conf_cmd : "unset");
    } else {
        klog("bad knock from %s (%d bytes)", ip, got);
        send(c, "N", 1, 0);
    }
}

int main(int argc, char **argv) {
    const char *conf = (argc > 1) ? argv[1] : "/var/opt/aa/knock.conf";
    load_conf(conf);
    if (!conf_cmd[0]) {
        klog("no SSHCMD configured — exiting (fail-safe: nothing to arm)");
        return 1;
    }
    RSA *rsa = load_pubkey(conf_pub);
    if (!rsa) {
        klog("no usable public key at %s — exiting (fail-safe)", conf_pub);
        return 1;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)atoi(conf_port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 4) < 0) {
        klog("bind/listen :%s failed: %s", conf_port, strerror(errno));
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);   /* client RST must not kill us on send */

    /* daemonize + reap children */
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN; sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);
    if (fork() > 0) return 0;             /* parent returns; child serves */
    setsid();
    int dn = open("/dev/null", O_RDWR);
    if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }

    klog("knockd listening on :%s (pub=%s cmd=%.60s)", conf_port, conf_pub, conf_cmd);
    for (;;) {
        struct sockaddr_in p; socklen_t pl = sizeof p;
        int c = accept(s, (struct sockaddr *)&p, &pl);
        if (c < 0) { if (errno == EINTR) continue; sleep(1); continue; }
        handle(c, rsa, &p);
        close(c);
    }
}
