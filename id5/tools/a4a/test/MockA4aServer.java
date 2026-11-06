// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// A stand-in A4A server for test_a4a_probe.sh: the same Etch bindings on the listening side. Behaviour:
//   ver_getVersion            9.8.7
//   rhmi_getCapabilities      SecurityException 1 "not logged in"
//   sas_certificate           a 4-byte challenge for the bytes "CERT", else IllegalArgumentException 2
//   av_create                 handle 7 after sas_certificate on the same connection, else SecurityException 3
//   av_dispose                logs "mock av_dispose <handle>"
// Prints "mock listening <port>" when up. Usage: MockA4aServer <port>

import de.bmw.idrive.BMWRemoting;
import de.bmw.idrive.BMWRemotingHelper;
import de.bmw.idrive.BaseBMWRemotingServer;
import org.apache.etch.bindings.java.support.ServerFactory;
import org.apache.etch.util.core.io.Transport;

import java.util.Arrays;

public class MockA4aServer {
    static class Server extends BaseBMWRemotingServer {
        boolean sawCert;

        @Override
        public BMWRemoting.VersionInfo ver_getVersion() {
            return new BMWRemoting.VersionInfo(9, 8, 7);
        }

        @Override
        public java.util.Map<?, ?> rhmi_getCapabilities(String component, Integer id) throws BMWRemoting.SecurityException {
            throw new BMWRemoting.SecurityException(1, "not logged in");
        }

        @Override
        public byte[] sas_certificate(byte[] cert) throws BMWRemoting.IllegalArgumentException {
            if (!Arrays.equals(cert, "CERT".getBytes())) throw new BMWRemoting.IllegalArgumentException(2, "bad cert");
            sawCert = true;
            return new byte[] { 1, 2, 3, 4 };
        }

        @Override
        public Integer av_create(Integer instance, String id) throws BMWRemoting.SecurityException {
            if (!sawCert) throw new BMWRemoting.SecurityException(3, "no session");
            return 7;
        }

        @Override
        public void av_dispose(Integer handle) {
            System.out.println("mock av_dispose " + handle);
        }
    }

    public static void main(String[] args) throws Exception {
        String uri = "tcp://127.0.0.1:" + args[0];
        ServerFactory listener = BMWRemotingHelper.newListener(uri, null, client -> new Server());
        listener.transportControl(Transport.START_AND_WAIT_UP, 4000);
        System.out.println("mock listening " + args[0]);
        Thread.sleep(Long.MAX_VALUE);
    }
}
