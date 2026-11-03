# libNbtService.so — the SSH-gate PluginApp (IMPROVEMENT-PLAN A1 + A5).
#
#   make test    host test: builds the plugin, the fake framework and a real knockd, drives all three
#   make db      regenerate NbtService_ALL_Texts.zip / _ALL_Images.zip
#   make car     cross-build libNbtService.so for the HU through the QNX docker image
#   make clean
#
# The car build must match HmiMain3D's C++ runtime: the _gpp variant (GNU libstdc++), PIC, and the
# car's own libAppFramework / libCoreFramework from milestone-7/rhmiapp/lib — exactly like the
# rhmiapp target in milestone-7/Makefile. Never the daemon's default (Dinkum) variant.

QNX_IMAGE ?= andreluizpd/qnx-qcc-docker:6.5.0
REPO       = $(abspath ../../..)
RHMI_LIBS  = ../../../milestone-7/rhmiapp/lib/libAppFramework.A.so ../../../milestone-7/rhmiapp/lib/libCoreFramework.A.so
CARFLAGS   = -O2 -Wall -Wextra -D_QNX_SOURCE -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp -fPIC

.PHONY: test db car clean

test:
	python3 test_nbtservice.py

db:
	python3 make_db.py

car:
	docker run --rm --platform linux/386 -v $(REPO):/src -w /src/id5/tools/nbtservice $(QNX_IMAGE) \
	  qcc -Vgcc_ntoarmv7le_gpp $(CARFLAGS) -shared -Wl,-soname,libNbtService.so \
	      -o libNbtService.so nbt_service_app.cpp $(RHMI_LIBS) -Wl,--allow-shlib-undefined -lang-c++
	@echo "built libNbtService.so — install with NbtService.manifest, NbtService.xml and the two zips"

clean:
	rm -f libNbtService.so
