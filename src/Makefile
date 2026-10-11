CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror -fPIC
LDFLAGS ?=

.PHONY: all check clean install rollback

all: libbcm-cv-host-event-shim.so

tools/sync_probe: tools/sync_probe.c bcm_cv_host_event_shim.c bcm_cv_host_event_shim.h
	$(CC) -O2 -g -Wall -Wextra -Werror $$(pkg-config --cflags libusb-1.0) \
		-o $@ tools/sync_probe.c bcm_cv_host_event_shim.c \
		$$(pkg-config --libs libusb-1.0) -ldl -pthread

libbcm-cv-host-event-shim.so: bcm_cv_host_event_shim.c bcm_cv_host_event_shim.h
	$(CC) $(CFLAGS) -shared $(LDFLAGS) -o $@ bcm_cv_host_event_shim.c -ldl -pthread

tests/libfakeusb.so: tests/fake_usb.c tests/fake_usb.h
	$(CC) $(CFLAGS) -shared $(LDFLAGS) -o $@ tests/fake_usb.c

tests/test_shim: tests/test_shim.c tests/fake_usb.h bcm_cv_host_event_shim.h tests/libfakeusb.so
	$(CC) -O2 -g -Wall -Wextra -Werror -o $@ tests/test_shim.c \
		-L. -lbcm-cv-host-event-shim -Ltests -lfakeusb \
		-Wl,-rpath,'$$ORIGIN/..:$$ORIGIN'

check: libbcm-cv-host-event-shim.so tests/libfakeusb.so tests/test_shim
	tests/test_shim

install: check
	./install.sh

rollback:
	./rollback.sh

clean:
	rm -f libbcm-cv-host-event-shim.so tests/libfakeusb.so tests/test_shim tools/sync_probe
