#!/bin/sh
set -eu

test -f package/doorfast/Makefile
test -f package/doorfast/files/doorfast.init
test -f package/doorfast/files/doorfast-recorder.init
test -f package/doorfast/files/doorfast-site-inventory.sh
grep -q 'doorfast-site-inventory.*usr/libexec/doorfast' package/doorfast/Makefile
grep -q 'doorfast-site-inventory.impl.sh' scripts/prepare-sdk-package.sh
grep -q 'doorfast-recorder.*usr/sbin/doorfast-recorder' package/doorfast/Makefile
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
grep -q 'PKGARCH:=x86_64' package/doorfast/Makefile
grep -q 'PKGARCH:=all' package/luci-app-doorfast/Makefile
grep -q '+doorfast +luci-base +rpcd' package/luci-app-doorfast/Makefile
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
! grep -R -E -q 'service|set|delete|add|exec|command' package/luci-app-doorfast/root/usr/share/rpcd/acl.d
node --check package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js
grep -q 'scripts/feeds install libpcap libuci libjson-c libopenssl' .github/workflows/build-apk.yml
grep -q 'actions/cache@v4' .github/workflows/build-apk.yml
grep -q 'doorfast-\*.apk' .github/workflows/build-apk.yml
! grep -q 'bin/packages/\*\*/\*.apk' .github/workflows/build-apk.yml
grep -q 'config_load doorfast' package/doorfast/files/doorfast.init
grep -q 'config_get_bool enabled main enabled 0' package/doorfast/files/doorfast.init
grep -F "config gvs 'main'" package/doorfast/files/doorfast.config
grep -F "option enabled '0'" package/doorfast/files/doorfast.config
grep -F "option gvs_interface ''" package/doorfast/files/doorfast.config
grep -F "option gvs_local_address ''" package/doorfast/files/doorfast.config
grep -F "option uplink_interface ''" package/doorfast/files/doorfast.config
grep -F "option sync_state_path '/etc/config/doorfast-sync'" package/doorfast/files/doorfast.config
grep -F "option passive_only '1'" package/doorfast/files/doorfast.config
grep -F "config state 'sync'" package/doorfast/files/doorfast-sync.config
grep -F "option version '0'" package/doorfast/files/doorfast-sync.config
grep -q 'doorfast-sync.config.*doorfast-sync' package/doorfast/Makefile
grep -q 'doorfast-deployment.config.*doorfast-deployment' package/doorfast/Makefile
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
