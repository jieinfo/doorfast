#!/bin/sh
set -eu

test -f package/doorfast/Makefile
test -f package/doorfast/files/doorfast.init
test -f package/doorfast/files/doorfast-group
test -f package/doorfast/files/doorfast-recorder.init
test -f package/doorfast/files/doorfast-event-relay.init
test -f package/doorfast/files/doorfast-events.config
test -f package/doorfast/files/doorfast-site-inventory.sh
grep -q 'doorfast-site-inventory.*usr/libexec/doorfast' package/doorfast/Makefile
grep -q 'doorfast-site-inventory.impl.sh' scripts/prepare-sdk-package.sh
grep -q 'doorfast-recorder.*usr/sbin/doorfast-recorder' package/doorfast/Makefile
grep -q 'doorfast-pcm-submit.*usr/sbin/doorfast-pcm-submit' package/doorfast/Makefile
grep -q 'doorfast-event-relay.*usr/sbin/doorfast-event-relay' package/doorfast/Makefile
grep -q 'doorfast-events.config.*doorfast-events' package/doorfast/Makefile
! grep -q 'respawn' package/doorfast/files/doorfast-recorder.init
test -f package/doorfast/files/doorfast.config
test -f package/doorfast/files/doorfast-sync.config
test -f package/doorfast/files/doorfast-deployment.config
test -f scripts/prepare-sdk-package.sh
test -f package/luci-app-doorfast/Makefile
test -f package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json
test -f package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json
test -f package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js
test -f package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js
test -f package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/settings.js
test -f package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/deployment.js
test -f package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/relay.js
test -f package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/logs.js
grep -q 'PKGARCH:=x86_64' package/doorfast/Makefile
grep -q 'PKGARCH:=all' package/luci-app-doorfast/Makefile
grep -q 'PKG_RELEASE:=13' package/luci-app-doorfast/Makefile
grep -q '+doorfast +luci-base +rpcd' package/luci-app-doorfast/Makefile
grep -q 'deployment configuration pages' package/luci-app-doorfast/Makefile
grep -Fq 'deployment.js $(1)/www/luci-static/resources/view/doorfast/deployment.js' package/luci-app-doorfast/Makefile
grep -Fq 'relay.js $(1)/www/luci-static/resources/view/doorfast/relay.js' package/luci-app-doorfast/Makefile
grep -Fq 'logs.js $(1)/www/luci-static/resources/view/doorfast/logs.js' package/luci-app-doorfast/Makefile
grep -q '+libubus +libubox +libblobmsg-json' package/doorfast/Makefile
grep -q -- '-DDF_WITH_UBUS' package/doorfast/Makefile
grep -q -- '-lubus -lubox -lblobmsg_json' package/doorfast/Makefile
grep -q 'package/luci-app-doorfast' scripts/prepare-sdk-package.sh
grep -q 'feeds install.*luci-base' .github/workflows/build-apk.yml
grep -q 'package/luci-app-doorfast/compile' .github/workflows/build-apk.yml
grep -q 'luci-app-doorfast-\*.apk' .github/workflows/build-apk.yml
! grep -R -E -q 'wget -O-|auth|auto_update|opkg|\.ipk' package/doorfast scripts
python3 -m json.tool package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json >/dev/null
python3 -m json.tool package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json >/dev/null
python3 - <<'PY'
import json

acl = json.load(open('package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json'))['luci-app-doorfast']
assert acl['read']['uci'] == ['doorfast', 'doorfast-automation', 'doorfast-events']
assert acl['write']['uci'] == ['doorfast', 'doorfast-automation', 'doorfast-events']
assert set(acl['read']['ubus']['doorfast']) == {'status', 'logs'}
assert 'ubus' not in acl['write']
assert set(acl['read']) == {'uci', 'ubus'}
assert set(acl['write']) == {'uci'}

menu = json.load(open('package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json'))
assert menu['admin/services/doorfast']['action']['type'] == 'firstchild'
assert menu['admin/services/doorfast/status']['action']['path'] == 'doorfast/status'
assert menu['admin/services/doorfast/settings']['action']['path'] == 'doorfast/settings'
assert menu['admin/services/doorfast/deployment']['action']['path'] == 'doorfast/deployment'
assert menu['admin/services/doorfast/relay']['action']['path'] == 'doorfast/relay'
assert menu['admin/services/doorfast/logs']['action']['path'] == 'doorfast/logs'
PY
node --check package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/settings.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/deployment.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/relay.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/logs.js
node tests/test_luci_settings.js
node tests/test_luci_deployment.js
node tests/test_luci_relay.js
node tests/test_luci_logs.js
grep -q 'scripts/feeds install libpcap libuci libjson-c libopenssl' .github/workflows/build-apk.yml
grep -q 'actions/cache@v4' .github/workflows/build-apk.yml
grep -q 'cancel-in-progress: true' .github/workflows/build-apk.yml
grep -q 'sh tests/test_site_inventory.sh' .github/workflows/build-apk.yml
grep -q 'doorfast-\*.apk' .github/workflows/build-apk.yml
! grep -q 'bin/packages/\*\*/\*.apk' .github/workflows/build-apk.yml
grep -q 'config_load doorfast' package/doorfast/files/doorfast.init
grep -q 'config_get_bool enabled main enabled 0' package/doorfast/files/doorfast.init
grep -F "config gvs 'main'" package/doorfast/files/doorfast.config
grep -F "option enabled '0'" package/doorfast/files/doorfast.config
grep -F "option gvs_interface ''" package/doorfast/files/doorfast.config
grep -F "option passive_interface ''" package/doorfast/files/doorfast.config
grep -F "option host_interface ''" package/doorfast/files/doorfast.config
grep -F "option gvs_local_address ''" package/doorfast/files/doorfast.config
grep -F "option indoor_ipaddr ''" package/doorfast/files/doorfast.config
grep -F "option indoor_netmask ''" package/doorfast/files/doorfast.config
grep -F "option uplink_interface ''" package/doorfast/files/doorfast.config
grep -F "option sync_state_path '/etc/config/doorfast-sync'" package/doorfast/files/doorfast.config
grep -F "option passive_only '1'" package/doorfast/files/doorfast.config
grep -F "option media_enabled '0'" package/doorfast/files/doorfast.config
grep -F "option media_go2rtc_port '8554'" package/doorfast/files/doorfast.config
! grep -Fq 'media_rtsp_password' package/doorfast/files/doorfast.config
! grep -Fq 'media_relay_token' package/doorfast/files/doorfast.config
test -f package/doorfast/files/doorfast-automation.config
grep -F "config automation 'main'" package/doorfast/files/doorfast-automation.config
! grep -Fq 'option call_elev' package/doorfast/files/doorfast-automation.config
! grep -Fq 'call_elev' package/doorfast/files/doorfast.config
! grep -Fq 'call_elev_direction' package/doorfast/files/doorfast.config
grep -Eq 'doorfast-automation\.config.+/doorfast-automation' package/doorfast/Makefile
grep -Fq 'procd_add_reload_trigger doorfast doorfast-automation' package/doorfast/files/doorfast.init
grep -Fq -- '--call-elev "$call_elev"' package/doorfast/files/doorfast.init
sh tests/test_doorfast_init.sh
sh tests/test_event_relay_init.sh
grep -F "config state 'sync'" package/doorfast/files/doorfast-sync.config
grep -F "option version '0'" package/doorfast/files/doorfast-sync.config
grep -q 'doorfast-sync.config.*doorfast-sync' package/doorfast/Makefile
grep -q 'doorfast-deployment.config.*doorfast-deployment' package/doorfast/Makefile
grep -q 'doorfast-group.*etc/uci-defaults/doorfast-group' package/doorfast/Makefile
grep -q 'PKG_RELEASE:=50' package/doorfast/Makefile
grep -Fq "option token ''" package/doorfast/files/doorfast-events.config
grep -Fq 'store_token "$token" "$token_file"' package/doorfast/files/doorfast-event-relay.init
grep -Fq 'http://*) ;;' package/doorfast/files/doorfast-event-relay.init
grep -q '+ip-full' package/doorfast/Makefile
grep -Fq 'HOST_ADDRESS_STATE=' package/doorfast/files/doorfast.init
grep -Fq 'configure_host_address || return 1' package/doorfast/files/doorfast.init
grep -q 'doorfast-pcm-http.*usr/sbin/doorfast-pcm-http' package/doorfast/Makefile
python3 - <<'ACCEPTANCE'
import pathlib
package = pathlib.Path('package/doorfast/Makefile').read_text()
workflow = pathlib.Path('.github/workflows/build-apk.yml').read_text()
compile_block = package.split('define Build/Compile\n', 1)[1].split('endef', 1)[0]
commands = compile_block.replace('\\\n', ' ').splitlines()
acceptance = [line for line in commands if '-o $(PKG_BUILD_DIR)/doorfast-pcm-http-acceptance' in line]
assert len(acceptance) == 1, 'missing distinct acceptance target compiler command'
assert '$(TARGET_CC)' in acceptance[0] and '-DDF_PCM_HTTP_ACCEPTANCE' in acceptance[0]
assert 'src/pcm_http.c' in acceptance[0] and 'src/pcm_http_main.c' in acceptance[0]
installed = package.split('define Package/doorfast/install\n', 1)[1].split('endef', 1)[0]
assert 'doorfast-pcm-http-acceptance' not in installed, 'acceptance binary must stay out of APK'
assert '$(1)/usr/sbin/doorfast-pcm-http\n' in installed
assert "grep -Fxq '/usr/sbin/doorfast-pcm-http' \"$doorfast_list\"" in workflow
assert "! grep -Fq 'doorfast-pcm-http-acceptance' \"$doorfast_list\"" in workflow
assert 'cp \"$acceptance\" artifacts/doorfast-pcm-http-acceptance' in workflow
assert 'test \"${#sdk_dirs[@]}\" -eq 1' in workflow
assert 'test \"${#acceptance_files[@]}\" -eq 1' in workflow
assert 'test \"${#doorfast_apks[@]}\" -eq 1' in workflow
assert 'test \"${#luci_apks[@]}\" -eq 1' in workflow
upload = workflow.split('uses: actions/upload-artifact@v4', 1)[1]
assert 'artifacts/doorfast-pcm-http-acceptance' in upload
assert 'artifacts/doorfast.apk' in upload and 'artifacts/luci-app-doorfast.apk' in upload
assert '**/bin/packages/' not in upload
# Fresh idle daemons report zero generations; only talking/active statuses
# require a nonzero, matching TX generation. Target blobmsg parsing runs in VM.
adapter = pathlib.Path('src/pcm_http_ubus.c').read_text()
assert 'if (strcmp(session, "talking") == 0 || parsed.audio_tx_active)' in adapter, 'idle zero-generation status must remain parseable'
ACCEPTANCE
grep -Fq -- '-DDF_WITH_UBUS -DDF_PCM_HTTP_PROGRAM' package/doorfast/Makefile
grep -Fq 'src/pcm_http.c' package/doorfast/Makefile
grep -Fq 'src/pcm_http_ubus.c' package/doorfast/Makefile
grep -Fq 'src/pcm_http_main.c' package/doorfast/Makefile
grep -Fq 'src/runtime_id.c' package/doorfast/Makefile
grep -Fq 'src/gvs_pcm_ingress.c' package/doorfast/Makefile
grep -Fq -- '-lubus -lubox -lblobmsg_json' package/doorfast/Makefile
grep -Eq '^  USERID:=:doorfast$' package/doorfast/Makefile
grep -Fq 'EVENT_DIR=/var/run/doorfast' package/doorfast/files/doorfast.init
grep -Fq 'DF_EVENT_STREAM_DEFAULT_PATH "/var/run/doorfast/events.sock"' src/event_stream.h
grep -Fq 'group_add_next doorfast' package/doorfast/files/doorfast.init
grep -Fq 'group_exists doorfast || return 1' package/doorfast/files/doorfast.init
grep -Fq 'ensure_event_runtime || return 1' package/doorfast/files/doorfast.init
grep -Fq 'ls -ldn "$1"' package/doorfast/files/doorfast-event-relay.init
! grep -Fq 'stat -c' package/doorfast/files/doorfast-event-relay.init
grep -Fq '$(INSTALL_DIR) $(1)/etc/doorfast' package/doorfast/Makefile
grep -Fq 'chmod 0750 $(1)/etc/doorfast' package/doorfast/Makefile
grep -Fq 'mkdir -p /etc/doorfast || exit 1' package/doorfast/Makefile
grep -Fq 'chown root:doorfast' package/doorfast/files/doorfast.init
grep -Fq 'chmod 0750' package/doorfast/files/doorfast.init
grep -Fq 'chmod(path, 0660)' src/event_stream.c
grep -F "config inline 'main'" package/doorfast/files/doorfast-deployment.config
grep -F "option enabled '0'" package/doorfast/files/doorfast-deployment.config
grep -F "option recording_enabled '0'" package/doorfast/files/doorfast-deployment.config
grep -F "option evidence_root '/mnt/doorfast'" package/doorfast/files/doorfast-deployment.config
grep -F "option observation ''" package/doorfast/files/doorfast-deployment.config
grep -F "option reserve_mib '6144'" package/doorfast/files/doorfast-deployment.config
! grep -q -- '--preflight' package/doorfast/files/doorfast.init
grep -q 'define Package/doorfast/postinst' package/doorfast/Makefile
grep -Fq '[ -z "$${IPKG_INSTROOT}" ]' package/doorfast/Makefile
grep -Fq '[ "$${PKG_UPGRADE}" = "1" ]' package/doorfast/Makefile
grep -q '/etc/init.d/doorfast restart' package/doorfast/Makefile

reload_trace=''
trigger_name=''
stop() { reload_trace="${reload_trace}stop "; }
start() { reload_trace="${reload_trace}start"; }
procd_add_reload_trigger() { trigger_name="$1"; }
. package/doorfast/files/doorfast.init
reload_service
test "$reload_trace" = 'stop start'
service_triggers
test "$trigger_name" = 'doorfast'
