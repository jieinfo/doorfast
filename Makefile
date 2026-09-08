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
TEST_SOURCES := tests/test_main.c tests/test_config.c tests/test_runtime_config.c tests/test_sip.c tests/test_gvs_deadline.c tests/test_gvs_frame.c tests/test_gvs_identity.c tests/test_gvs_presence.c tests/test_gvs_serialize.c tests/test_gvs_sync.c tests/test_gvs_sync_state.c tests/test_gvs_sync_adapters.c tests/test_gvs_runtime_sync.c tests/test_gvs_observer.c tests/test_gvs_receive.c tests/test_gvs_replay.c tests/test_gvs_session.c tests/test_policy.c tests/test_capture.c tests/test_capture_retry.c tests/test_audit.c tests/test_discovery.c tests/test_diagnostics.c src/config.c src/runtime_config.c src/event.c src/sip.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c src/gvs_presence.c src/gvs_serialize.c src/gvs_sync.c src/gvs_sync_state.c src/gvs_sync_adapters.c src/gvs_runtime_sync.c src/gvs_observer.c src/gvs_packet.c src/gvs_receive.c src/gvs_replay.c src/gvs_session.c src/session.c src/policy.c src/capture.c src/capture_retry.c src/audit.c src/discovery.c src/diagnostics.c
DAEMON_SOURCES := src/main.c src/config.c src/runtime_config.c src/runtime_service.c src/event.c src/sip.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c src/gvs_presence.c src/gvs_serialize.c src/gvs_sync.c src/gvs_sync_state.c src/gvs_sync_adapters.c src/gvs_runtime_sync.c src/gvs_observer.c src/gvs_packet.c src/gvs_receive.c src/gvs_replay.c src/gvs_session.c src/session.c src/policy.c src/capture.c src/capture_retry.c src/audit.c src/discovery.c src/diagnostics.c

TEST_SOURCES += tests/test_gvs_priority.c src/gvs_priority.c
DAEMON_SOURCES += src/gvs_priority.c

.PHONY: test doorfast clean

test: build/doorfast-tests

doorfast: build/doorfast

build/doorfast-tests: $(TEST_SOURCES) tests/test.h src/doorfast.h src/gvs_priority.h

build:

	@mkdir -p build

build/doorfast-tests: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) -o $@ $(PCAP_LIBS)
	$@

build/doorfast: $(DAEMON_SOURCES) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DAEMON_SOURCES) -o $@ $(PCAP_LIBS)

clean:
	rm -rf build
