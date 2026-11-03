# Wi-Fi exploration tools. mwu-send runs on the HU; the watchers run on the laptop.
REPO      := $(abspath ../../..)
QNX_IMAGE ?= andreluizpd/qnx-qcc-docker:6.5.0

.PHONY: all test car clean
all: test car

mwu-send-host: mwu_send.c
	cc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -O2 -o $@ $<

test: mwu-send-host
	python3 fake_mwud.py ./mwu-send-host

# ARMv7LE QNX binary for /var/opt/aa/mwu-send
car:
	docker run --rm --platform linux/386 -v $(REPO):/src -w /src/id5/tools/wifi $(QNX_IMAGE) \
	  qcc -Vgcc_ntoarmv7le -O2 -Wall -Wextra -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp -o mwu-send mwu_send.c -lsocket
	@echo "built mwu-send — deploy with: car.sh push mwu-send /var/opt/aa/mwu-send"

clean:
	rm -f mwu-send mwu-send-host
