CC ?= cc
ifneq ($(wildcard /opt/homebrew/opt/libpcap/bin/pcap-config),)
PCAP_CONFIG ?= /opt/homebrew/opt/libpcap/bin/pcap-config
else
PCAP_CONFIG ?= pcap-config
endif
PCAP_CFLAGS ?= $(shell $(PCAP_CONFIG) --cflags 2>/dev/null)
PCAP_LIBS ?= $(shell $(PCAP_CONFIG) --libs 2>/dev/null)
CPPFLAGS := -D_DEFAULT_SOURCE
CFLAGS := -std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support $(PCAP_CFLAGS)
TEST_SOURCES := tests/test_main.c tests/test_config.c tests/test_runtime_config.c tests/test_sip.c tests/test_gvs_deadline.c tests/test_gvs_frame.c tests/test_gvs_identity.c tests/test_gvs_presence.c tests/test_gvs_reply_queue.c tests/test_gvs_send_transaction.c tests/test_gvs_memory_sender.c tests/test_gvs_call_command.c tests/test_gvs_serialize.c tests/test_gvs_sync.c tests/test_gvs_sync_state.c tests/test_gvs_sync_adapters.c tests/test_gvs_runtime_sync.c tests/test_runtime_ubus.c tests/test_runtime_service.c tests/test_gvs_observer.c tests/test_gvs_receive.c tests/test_gvs_replay.c tests/test_gvs_session.c tests/test_policy.c tests/test_capture.c tests/test_capture_retry.c tests/test_audit.c tests/test_discovery.c tests/test_diagnostics.c src/config.c src/runtime_config.c src/runtime_service.c src/event.c src/sip.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c src/gvs_presence.c src/gvs_reply_queue.c src/gvs_send_transaction.c src/gvs_memory_sender.c src/gvs_call_command.c src/gvs_serialize.c src/gvs_sync.c src/gvs_sync_state.c src/gvs_sync_adapters.c src/gvs_runtime_sync.c src/runtime_ubus.c src/gvs_observer.c src/gvs_packet.c src/gvs_receive.c src/gvs_replay.c src/gvs_session.c src/session.c src/policy.c src/capture.c src/capture_retry.c src/audit.c src/discovery.c src/diagnostics.c
DAEMON_SOURCES := src/main.c src/config.c src/runtime_config.c src/runtime_service.c src/event.c src/sip.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c src/gvs_presence.c src/gvs_reply_queue.c src/gvs_send_transaction.c src/gvs_memory_sender.c src/gvs_call_command.c src/gvs_serialize.c src/gvs_sync.c src/gvs_sync_state.c src/gvs_sync_adapters.c src/gvs_runtime_sync.c src/runtime_ubus.c src/gvs_observer.c src/gvs_packet.c src/gvs_receive.c src/gvs_replay.c src/gvs_session.c src/session.c src/policy.c src/capture.c src/capture_retry.c src/audit.c src/discovery.c src/diagnostics.c
SIM_SOURCES := tools/gvs-peer-sim.c tests/support/gvs_peer_sim.c \
	src/event.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c \
	src/gvs_observer.c src/gvs_presence.c src/gvs_priority.c \
	src/gvs_receive.c src/gvs_runtime_sync.c src/gvs_serialize.c \
	src/gvs_session.c src/gvs_sync.c src/gvs_sync_adapters.c
UDP_INJECT_SOURCES := tools/gvs-peer-udp-inject.c \
	$(filter-out tools/gvs-peer-sim.c,$(SIM_SOURCES))

TEST_SOURCES += tests/test_gvs_priority.c src/gvs_priority.c
TEST_SOURCES += tests/test_gvs_peer_sim.c tests/support/gvs_peer_sim.c
DAEMON_SOURCES += src/gvs_priority.c

.PHONY: test doorfast peer-sim peer-udp-inject clean

test: build/doorfast-tests

doorfast: build/doorfast

peer-sim: build/gvs-peer-sim

peer-udp-inject: build/gvs-peer-udp-inject

build/doorfast-tests: $(TEST_SOURCES) tests/test.h src/doorfast.h src/gvs_priority.h src/gvs_send_transaction.h src/gvs_memory_sender.h src/gvs_call_command.h

build:

	@mkdir -p build

build/doorfast-tests: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) -o $@ $(PCAP_LIBS)
	$@

build/doorfast: $(DAEMON_SOURCES) src/gvs_send_transaction.h src/gvs_memory_sender.h src/gvs_call_command.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DAEMON_SOURCES) -o $@ $(PCAP_LIBS)

build/gvs-peer-sim: $(SIM_SOURCES) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SIM_SOURCES) -o $@

build/gvs-peer-udp-inject: $(UDP_INJECT_SOURCES) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UDP_INJECT_SOURCES) -o $@

clean:
	rm -rf build
