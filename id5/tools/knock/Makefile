# knockd — the HU SSH gate. Two builds: the car (QNX ARMv7LE) and the host (tests).
#
#   make host           host binary + run the host test  (gcc, OpenSSL >= 1.1 ok)
#   make test           just the host test
#   make car            cross-build knockd for the HU through the QNX docker image
#   make clean
#
# The car build goes through Docker because QNX SDP 6.5.0 / qcc only exists there.
# QNX_IMAGE is the prebuilt licensed i386 image (see the project CLAUDE.md).
# libcrypto comes from the SDP itself; -static keeps knockd independent of which
# OpenSSL the HU happens to have on its library path.

QNX_IMAGE ?= andreluizpd/qnx-qcc-docker:6.5.0
REPO       = $(abspath ../../..)
CC        ?= gcc
HOSTFLAGS  = -std=gnu89 -Wall -Wextra -O1 -Wno-deprecated-declarations

.PHONY: host test car car-dynamic clean

host: knockd-host
	@echo "built knockd-host (host test binary; NOT for the car)"

knockd-host: knockd.c
	$(CC) $(HOSTFLAGS) -o $@ knockd.c -lcrypto

test:
	python3 test_knock.py

# Statically linked ARM binary for /var/opt/aa/knockd.
car:
	docker run --rm --platform linux/386 -v $(REPO):/src -w /src/id5/tools/knock $(QNX_IMAGE) \
	  qcc -Vgcc_ntoarmv7le -O2 -Wall -Wextra -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp \
	      -static -o knockd knockd.c -lcrypto -lsocket
	@echo "built knockd (ARMv7LE, static) — deploy with: car.sh push knockd /var/opt/aa/knockd.new"

car-dynamic:
	docker run --rm --platform linux/386 -v $(REPO):/src -w /src/id5/tools/knock $(QNX_IMAGE) \
	  qcc -Vgcc_ntoarmv7le -O2 -Wall -Wextra -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp \
	      -o knockd knockd.c -lcrypto -lsocket

clean:
	rm -f knockd knockd-host
