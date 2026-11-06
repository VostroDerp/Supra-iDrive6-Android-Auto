// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// A4aProbe: does a TCP port speak BMW's A4A protocol (Etch, the "BMWRemoting" interface MyBMW and AAIdrive use),
// and what does it allow before a login? Laptop side, for NEXT-SESSION step 5c: reach the head unit's port through
// `car.sh forward 14004 <port>`, then run this against 127.0.0.1:14004. Run it through a4a_probe.sh, which fetches
// the two libraries (IDriveConnectKit's Etch bindings, MIT, and the Etch runtime, Apache 2.0) and checks them.
//
//   a4a_probe.sh <host> <port>               connect, start Etch, ask the version (read-only)
//     --caps                                 also ask rhmi_getCapabilities("", 255) without a login (read-only)
//     --cert <file>                          also send <file> with sas_certificate and print the challenge. There is
//                                            no login: the challenge is never signed. The file is a certificate you
//                                            supply (as AAIdrive does), never one from this repo.
//     --try-av                               also try av_create without a login. If the car accepts it, the handle
//                                            is disposed at once (av_dispose). This one can change what the car
//                                            shows: run it only with a go.
//   a4a_probe.sh --raw <secs> <host> <port>  connect, send nothing, print what the server sends in <secs> (a port that
//                                            is not raw Etch, e.g. BCL)
//
// Every step prints one line: OK, REFUSED (the server answered with an error: class, errorId, errorMsg) or FAILED
// (no answer, timeout, transport error). Exit: 0 Etch answered, 1 no TCP connection, 2 usage, 3 TCP but no Etch.

import de.bmw.idrive.BMWRemoting;
import de.bmw.idrive.BMWRemotingHelper;
import de.bmw.idrive.BMWRemotingServer;
import de.bmw.idrive.BaseBMWRemotingClient;
import de.bmw.idrive.RemoteBMWRemotingServer;

import java.io.InputStream;
import java.lang.reflect.Field;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public class A4aProbe {
    static final int CALL_TIMEOUT_MS = 5000;
    static final ExecutorService POOL = Executors.newCachedThreadPool(r -> {
        Thread t = new Thread(r);
        t.setDaemon(true);
        return t;
    });

    // The client side the car can call back into. The probe registers nothing, so callbacks are only logged.
    static class LoggingClient extends BaseBMWRemotingClient {
        @Override
        public void av_connectionGranted(Integer handle, BMWRemoting.AVConnectionType type) {
            System.out.println("callback av_connectionGranted " + handle + " " + type);
        }
        @Override
        public void av_connectionDenied(Integer handle, BMWRemoting.AVConnectionType type) {
            System.out.println("callback av_connectionDenied " + handle + " " + type);
        }
    }

    static void usage() {
        System.err.println("usage: a4a_probe.sh <host> <port> [--caps] [--cert <file>] [--try-av]");
        System.err.println("       a4a_probe.sh --raw <secs> <host> <port>");
        System.exit(2);
    }

    public static void main(String[] args) throws Exception {
        if (args.length >= 1 && args[0].equals("--raw")) {
            if (args.length != 4) usage();
            System.exit(raw(args[2], parsePort(args[3]), Integer.parseInt(args[1])));
        }
        if (args.length < 2) usage();
        final String host = args[0];
        final int port = parsePort(args[1]);
        boolean caps = false, tryAv = false;
        String certFile = null;
        for (int i = 2; i < args.length; ++i) {
            if (args[i].equals("--caps")) caps = true;
            else if (args[i].equals("--try-av")) tryAv = true;
            else if (args[i].equals("--cert") && i + 1 < args.length) certFile = args[++i];
            else usage();
        }
        final byte[] cert = certFile == null ? null : Files.readAllBytes(Paths.get(certFile));

        if (!tcpOpen(host, port)) {
            System.out.println("tcp      FAILED nothing accepts " + host + ":" + port);
            System.exit(1);
        }
        System.out.println("tcp      OK " + host + ":" + port + " accepts");

        final RemoteBMWRemotingServer server;
        try {
            server = call(() -> {
                String uri = "tcp://" + host + ":" + port + "?Packetizer.maxPktSize=8388608&TcpTransport.noDelay=true";
                RemoteBMWRemotingServer s = BMWRemotingHelper.newServer(uri, null, rc -> new LoggingClient());
                s._startAndWaitUp(4000);
                return s;
            });
        } catch (Exception e) {
            System.out.println("etch     FAILED " + describe(e));
            System.out.println("verdict  TCP but no Etch session: try --raw to see what the port sends");
            System.exit(3);
            return;
        }
        System.out.println("etch     OK transport up");

        // The transport comes up on the TCP connection alone. The first call shows whether the far end speaks Etch.
        boolean etch = step("version", () -> {
            BMWRemoting.VersionInfo v = server.ver_getVersion();
            return v == null ? "null" : v.major + "." + v.minor + "." + v.revision;
        });
        if (caps) etch |= step("caps", () -> sorted(server.rhmi_getCapabilities("", 255)));
        if (cert != null) {
            etch |= step("cert", () -> {
                byte[] challenge = server.sas_certificate(cert);
                return (challenge == null ? 0 : challenge.length) + " byte challenge " + hex(challenge, 64)
                        + " (not signed, no login)";
            });
        }
        if (tryAv) {
            etch |= step("av", () -> {
                Integer h = server.av_create(0, "nbt-a4a-probe");
                String disposed;
                try {
                    server.av_dispose(h);
                    disposed = "disposed";
                } catch (Exception e) {
                    disposed = "av_dispose " + describe(e);
                }
                return "av_create accepted without a login: handle " + h + ", " + disposed;
            });
        }
        try {
            server._stopAndWaitDown(2000);
        } catch (Exception e) {
            // the connection is dropped on exit anyway
        }
        System.out.println(etch ? "verdict  Etch answered (OK or REFUSED above): this port is an A4A server"
                                : "verdict  no Etch answer to any call");
        System.exit(etch ? 0 : 3);
    }

    // One call: OK <result>, REFUSED <server error>, FAILED <no answer>. True if the server answered at all.
    static boolean step(String name, Callable<Object> c) {
        String label = String.format("%-8s ", name);
        try {
            Object r = call(c);
            System.out.println(label + "OK " + r);
            return true;
        } catch (Exception e) {
            if (isServerError(e)) {
                System.out.println(label + "REFUSED " + describe(e));
                return true;
            }
            System.out.println(label + "FAILED " + describe(e));
            return false;
        }
    }

    static <T> T call(Callable<T> c) throws Exception {
        Future<T> f = POOL.submit(c);
        try {
            return f.get(CALL_TIMEOUT_MS, TimeUnit.MILLISECONDS);
        } catch (ExecutionException e) {
            Throwable t = e.getCause();
            if (t instanceof Exception) throw (Exception) t;
            throw e;
        } catch (TimeoutException e) {
            f.cancel(true);
            throw new TimeoutException("no answer in " + CALL_TIMEOUT_MS + " ms");
        }
    }

    // The exceptions BMWRemoting declares (SecurityException, IllegalArgumentException, ...) come from the server.
    static boolean isServerError(Throwable t) {
        return t.getClass().getName().startsWith("de.bmw.idrive.BMWRemoting$");
    }

    static String describe(Throwable t) {
        StringBuilder b = new StringBuilder(t.getClass().getSimpleName());
        for (String f : new String[] { "errorId", "errorMsg" }) {
            try {
                Field fld = t.getClass().getField(f);
                b.append(' ').append(f).append('=').append(fld.get(t));
            } catch (ReflectiveOperationException e) {
                // not a BMWRemoting exception
            }
        }
        if (!isServerError(t) && t.getMessage() != null) b.append(": ").append(t.getMessage());
        return b.toString();
    }

    static Object sorted(Map<?, ?> m) {
        if (m == null) return "null";
        Map<String, Object> s = new TreeMap<>();
        for (Map.Entry<?, ?> e : m.entrySet()) s.put(String.valueOf(e.getKey()), e.getValue());
        return s;
    }

    static boolean tcpOpen(String host, int port) {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(host, port), 3000);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // Connect, send nothing, print what arrives in <secs>.
    static int raw(String host, int port, int secs) {
        byte[] buf = new byte[4096];
        int n = 0;
        boolean closed = false;
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(host, port), 3000);
            System.out.println("tcp      OK " + host + ":" + port + " accepts, listening " + secs + " s, sending nothing");
            long end = System.currentTimeMillis() + secs * 1000L;
            InputStream in = s.getInputStream();
            while (n < buf.length) {
                long left = end - System.currentTimeMillis();
                if (left <= 0) break;
                s.setSoTimeout((int) left);
                try {
                    int r = in.read(buf, n, buf.length - n);
                    if (r < 0) {
                        closed = true;
                        break;
                    }
                    n += r;
                } catch (SocketTimeoutException e) {
                    break;
                }
            }
        } catch (Exception e) {
            System.out.println("tcp      FAILED " + describe(e));
            return 1;
        }
        byte[] got = new byte[n];
        System.arraycopy(buf, 0, got, 0, n);
        System.out.println("raw      " + n + " bytes" + (closed ? ", then the server closed" : "")
                + (n > 0 ? ": " + hex(got, 512) : ""));
        if (n > 0) System.out.println("ascii    " + ascii(got, 512));
        return 0;
    }

    static int parsePort(String s) {
        int p = Integer.parseInt(s);
        if (p < 1 || p > 65535) usage();
        return p;
    }

    static String hex(byte[] b, int max) {
        if (b == null) return "-";
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < b.length && i < max; ++i) s.append(String.format("%02x", b[i] & 0xff));
        if (b.length > max) s.append("...");
        return s.toString();
    }

    static String ascii(byte[] b, int max) {
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < b.length && i < max; ++i) {
            int c = b[i] & 0xff;
            s.append(c >= 0x20 && c < 0x7f ? (char) c : '.');
        }
        return s.toString();
    }
}
