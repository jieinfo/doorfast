CC ?= cc
ifneq ($(wildcard /opt/homebrew/opt/libpcap/bin/pcap-config),)
PCAP_CONFIG ?= /opt/homebrew/opt/libpcap/bin/pcap-config
else
PCAP_CONFIG ?= pcap-config
endif
PCAP_CFLAGS ?= $(shell $(PCAP_CONFIG) --cflags 2>/dev/null)
PCAP_LIBS ?= $(shell $(PCAP_CONFIG) --libs 2>/dev/null)
CPPFLAGS := -D_DEFAULT_SOURCE -DDF_ALLOW_TEST_ROOT
CFLAGS := -std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support $(PCAP_CFLAGS)
TEST_SOURCES := tests/test_main.c tests/test_event_relay.c src/event_relay.c tests/test_config.c tests/test_runtime_config.c tests/test_sip.c tests/test_gvs_deadline.c tests/test_gvs_frame.c tests/test_gvs_identity.c tests/test_gvs_presence.c tests/test_gvs_reply_queue.c tests/test_gvs_send_transaction.c tests/test_gvs_memory_sender.c tests/test_gvs_call_command.c tests/test_gvs_serialize.c tests/test_gvs_sync.c tests/test_gvs_sync_state.c tests/test_gvs_sync_adapters.c tests/test_gvs_runtime_sync.c tests/test_runtime_ubus.c tests/test_runtime_service.c tests/test_event_stream.c tests/test_gvs_observer.c tests/test_gvs_receive.c tests/test_gvs_replay.c tests/test_gvs_session.c tests/test_policy.c tests/test_capture.c tests/test_capture_retry.c tests/test_audit.c tests/test_discovery.c tests/test_diagnostics.c src/config.c src/runtime_config.c src/runtime_service.c src/event_stream.c src/event.c src/sip.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c src/gvs_presence.c src/gvs_reply_queue.c src/gvs_send_transaction.c src/gvs_memory_sender.c src/gvs_call_command.c src/gvs_serialize.c src/gvs_sync.c src/gvs_sync_state.c src/gvs_sync_adapters.c src/gvs_runtime_sync.c src/runtime_ubus.c src/gvs_observer.c src/gvs_packet.c src/gvs_receive.c src/gvs_replay.c src/gvs_session.c src/session.c src/policy.c src/capture.c src/capture_retry.c src/audit.c src/discovery.c src/diagnostics.c
DAEMON_SOURCES := src/main.c src/config.c src/runtime_config.c src/runtime_service.c src/event_stream.c src/event.c src/sip.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c src/gvs_presence.c src/gvs_reply_queue.c src/gvs_send_transaction.c src/gvs_memory_sender.c src/gvs_call_command.c src/gvs_serialize.c src/gvs_sync.c src/gvs_sync_state.c src/gvs_sync_adapters.c src/gvs_runtime_sync.c src/runtime_ubus.c src/gvs_observer.c src/gvs_packet.c src/gvs_receive.c src/gvs_replay.c src/gvs_session.c src/session.c src/policy.c src/capture.c src/capture_retry.c src/audit.c src/discovery.c src/diagnostics.c
SIM_SOURCES := tools/gvs-peer-sim.c tests/support/gvs_peer_sim.c \
	src/event.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c \
	src/gvs_observer.c src/gvs_presence.c src/gvs_priority.c \
	src/gvs_receive.c src/gvs_runtime_sync.c src/gvs_serialize.c \
	src/gvs_session.c src/gvs_sync.c src/gvs_sync_adapters.c
UDP_INJECT_SOURCES := tools/gvs-peer-udp-inject.c \
	$(filter-out tools/gvs-peer-sim.c,$(SIM_SOURCES))
PCM_SUBMIT_SOURCES := src/pcm_submit_main.c src/gvs_pcm_ingress.c

TEST_SOURCES += tests/test_gvs_priority.c src/gvs_priority.c
TEST_SOURCES += tests/test_gvs_peer_sim.c tests/support/gvs_peer_sim.c
DAEMON_SOURCES += src/gvs_priority.c
TEST_SOURCES += tests/test_gvs_call_dispatch.c src/gvs_call_dispatch.c
TEST_SOURCES += tests/test_gvs_transport_policy.c src/gvs_transport_policy.c
TEST_SOURCES += tests/test_gvs_call_ack.c src/gvs_call_ack.c
TEST_SOURCES += tests/test_gvs_call_runtime.c src/gvs_call_runtime.c
TEST_SOURCES += src/gvs_udp_sender.c
TEST_SOURCES += tests/test_gvs_udp_sender.c
TEST_SOURCES += tests/test_gvs_call_control.c src/gvs_call_control.c
TEST_SOURCES += tests/test_gvs_incoming_reply.c src/gvs_incoming_reply.c
TEST_SOURCES += tests/test_gvs_handshake.c src/gvs_handshake.c
TEST_SOURCES += tests/test_gvs_vendor_header.c src/gvs_vendor_header.c
TEST_SOURCES += tests/test_gvs_access.c src/gvs_access.c
TEST_SOURCES += tests/test_gvs_elevator.c src/gvs_elevator.c
TEST_SOURCES += tests/test_gvs_elevator_control.c src/gvs_elevator_control.c
TEST_SOURCES += tests/test_gvs_elevator_query.c src/gvs_elevator_query.c
TEST_SOURCES += tests/test_gvs_media.c src/gvs_media.c
TEST_SOURCES += tests/test_gvs_media_admission.c src/gvs_media_admission.c
TEST_SOURCES += tests/test_gvs_media_lifecycle.c src/gvs_media_lifecycle.c
TEST_SOURCES += tests/test_gvs_video_reassembly.c src/gvs_video_reassembly.c
TEST_SOURCES += tests/test_gvs_jpeg.c
TEST_SOURCES += tests/test_gvs_video_frame_cache.c src/gvs_video_frame_cache.c
TEST_SOURCES += tests/test_gvs_video_snapshot.c src/gvs_video_snapshot.c
TEST_SOURCES += tests/test_gvs_audio_buffer.c src/gvs_audio_buffer.c
TEST_SOURCES += tests/test_gvs_audio_tx.c src/gvs_audio_tx.c
TEST_SOURCES += tests/test_gvs_pcm_ingress.c src/gvs_pcm_ingress.c
TEST_SOURCES += src/gvs_pcm_pump.c
TEST_SOURCES += tests/test_g711_alaw.c src/g711_alaw.c
TEST_SOURCES += tests/test_gvs_audio_chunk_store.c src/gvs_audio_chunk_store.c
TEST_SOURCES += tests/test_runtime_id.c src/runtime_id.c
TEST_SOURCES += tests/test_pcm_http.c src/pcm_http.c
build/doorfast-tests: src/pcm_http.h
DAEMON_SOURCES += src/runtime_id.c
DAEMON_SOURCES += src/gvs_video_snapshot.c
DAEMON_SOURCES += src/gvs_media.c src/gvs_video_reassembly.c
DAEMON_SOURCES += src/gvs_video_frame_cache.c
DAEMON_SOURCES += src/gvs_media_admission.c
DAEMON_SOURCES += src/gvs_media_lifecycle.c
DAEMON_SOURCES += src/gvs_audio_buffer.c
DAEMON_SOURCES += src/gvs_audio_tx.c
DAEMON_SOURCES += src/gvs_pcm_ingress.c
DAEMON_SOURCES += src/gvs_pcm_pump.c
DAEMON_SOURCES += src/g711_alaw.c
DAEMON_SOURCES += src/gvs_audio_chunk_store.c
TEST_SOURCES += tests/test_deployment_config.c src/deployment_config.c
TEST_SOURCES += tests/test_deployment_preflight.c src/deployment_preflight.c
TEST_SOURCES += tests/test_deployment_snapshot.c src/deployment_snapshot.c
TEST_SOURCES += tests/test_pcap_ring.c src/pcap_ring.c
TEST_SOURCES += tests/test_gvs_packet.c tests/test_evidence_classifier.c src/evidence_classifier.c
TEST_SOURCES += tests/test_evidence_log.c src/evidence_log.c
TEST_SOURCES += src/evidence_metadata.c
TEST_SOURCES += src/deployment_health.c
DAEMON_SOURCES += src/deployment_health.c
TEST_SOURCES += tests/test_evidence_recorder.c src/evidence_recorder.c
build/doorfast-tests: src/evidence_recorder.h
build/doorfast-tests: src/evidence_classifier.h
build/doorfast-tests: src/evidence_log.h
DAEMON_SOURCES += src/deployment_config.c src/deployment_preflight.c src/deployment_snapshot.c src/deployment_report.c
build/doorfast-tests: src/deployment_preflight.h
build/doorfast-tests: src/deployment_snapshot.h
build/doorfast-tests: src/pcap_ring.h
build/doorfast: src/deployment_config.h src/deployment_preflight.h src/deployment_snapshot.h src/deployment_report.h
DAEMON_SOURCES += src/gvs_call_runtime.c
DAEMON_SOURCES += src/gvs_udp_sender.c
DAEMON_SOURCES += src/gvs_transport_policy.c
DAEMON_SOURCES += src/gvs_call_control.c
DAEMON_SOURCES += src/gvs_incoming_reply.c
DAEMON_SOURCES += src/gvs_handshake.c
DAEMON_SOURCES += src/gvs_vendor_header.c
DAEMON_SOURCES += src/gvs_access.c
DAEMON_SOURCES += src/gvs_elevator.c
DAEMON_SOURCES += src/gvs_elevator_control.c
DAEMON_SOURCES += src/gvs_elevator_query.c
build/doorfast-tests build/doorfast: src/gvs_call_runtime.h
build/doorfast-tests build/doorfast: src/gvs_call_control.h src/deployment_config.h
build/doorfast-tests build/doorfast: src/gvs_audio_tx.h
build/doorfast-tests build/doorfast: src/gvs_media_admission.h
build/doorfast-tests build/doorfast: src/gvs_media_lifecycle.h
build/doorfast-tests build/doorfast: src/gvs_video_frame_cache.h
build/doorfast-tests build/doorfast: src/gvs_pcm_ingress.h
build/doorfast-tests build/doorfast: src/gvs_pcm_pump.h
DAEMON_SOURCES += src/gvs_call_ack.c
build/doorfast-tests build/doorfast: src/gvs_call_ack.h
DAEMON_SOURCES += src/gvs_call_dispatch.c
build/doorfast-tests build/doorfast: src/gvs_call_dispatch.h

.PHONY: test doorfast peer-sim peer-udp-inject pcm-submit clean

RECORDER_SOURCES := src/recorder_main.c src/evidence_recorder.c src/pcap_ring.c src/evidence_classifier.c src/capture.c src/gvs_packet.c src/gvs_frame.c src/event.c src/deployment_config.c src/deployment_preflight.c src/deployment_snapshot.c src/runtime_config.c src/config.c src/gvs_identity.c
recorder: build/doorfast-recorder
RECORDER_SOURCES += src/recorder_selftest.c
RECORDER_SOURCES += src/evidence_metadata.c src/evidence_log.c
build/doorfast-recorder: $(RECORDER_SOURCES) | build
	$(CC) $(CPPFLAGS) -DDF_RECORDER_PROGRAM $(CFLAGS) $(RECORDER_SOURCES) -o $@ $(PCAP_LIBS)

test: build/doorfast-tests

doorfast: build/doorfast

peer-sim: build/gvs-peer-sim

peer-udp-inject: build/gvs-peer-udp-inject

pcm-submit: build/doorfast-pcm-submit

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

build/doorfast-pcm-submit: $(PCM_SUBMIT_SOURCES) src/gvs_pcm_ingress.h | build
	$(CC) $(CPPFLAGS) -DDF_PCM_SUBMIT_PROGRAM $(CFLAGS) $(PCM_SUBMIT_SOURCES) -o $@

clean:
	rm -rf build
