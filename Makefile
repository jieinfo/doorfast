CC ?= cc
ifneq ($(wildcard /opt/homebrew/opt/libpcap/bin/pcap-config),)
PCAP_CONFIG ?= /opt/homebrew/opt/libpcap/bin/pcap-config
else
PCAP_CONFIG ?= pcap-config
endif
PCAP_CFLAGS ?= $(shell $(PCAP_CONFIG) --cflags 2>/dev/null)
PCAP_LIBS ?= $(shell $(PCAP_CONFIG) --libs 2>/dev/null)
CPPFLAGS := -D_DEFAULT_SOURCE
CFLAGS := -std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests $(PCAP_CFLAGS)
TEST_SOURCES := tests/test_main.c tests/test_config.c tests/test_sip.c tests/test_policy.c tests/test_capture.c tests/test_audit.c tests/test_discovery.c tests/test_diagnostics.c src/config.c src/event.c src/sip.c src/session.c src/policy.c src/capture.c src/audit.c src/discovery.c src/diagnostics.c
DAEMON_SOURCES := src/main.c src/config.c src/event.c src/sip.c src/session.c src/policy.c src/capture.c src/audit.c src/discovery.c src/diagnostics.c

.PHONY: test doorfast clean

test: build/doorfast-tests

doorfast: build/doorfast

build/doorfast-tests: $(TEST_SOURCES) tests/test.h src/doorfast.h

build:

	@mkdir -p build

build/doorfast-tests: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) -o $@ $(PCAP_LIBS)
	$@

build/doorfast: $(DAEMON_SOURCES) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DAEMON_SOURCES) -o $@ $(PCAP_LIBS)

clean:
	rm -rf build
