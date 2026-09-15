include $(TOPDIR)/rules.mk

PKG_NAME:=doorfast
PKG_VERSION:=0.1.0
PKG_RELEASE:=38
PKGARCH:=x86_64
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/doorfast
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Doorfast GVS integration daemon
  DEPENDS:=+libpcap +libuci +libjson-c +libopenssl +libubus +libubox +libblobmsg-json +jsonfilter
  USERID:=:doorfast
endef

define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/src
	$(CP) ./src/. $(PKG_BUILD_DIR)/src/
	$(CP) ./src/event_relay_main.c $(PKG_BUILD_DIR)/event_relay_main.c
	$(RM) $(PKG_BUILD_DIR)/src/event_relay_main.c
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE -DDF_WITH_UBUS -std=c17 -Wall -Wextra -Werror -pedantic \
		-I$(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/src/*.c -lpcap -lubus -lubox -lblobmsg_json -o $(PKG_BUILD_DIR)/doorfast
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE -DDF_WITH_UBUS -DDF_RECORDER_PROGRAM -std=c17 -Wall -Wextra -Werror -pedantic \
		-I$(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/src/*.c -lpcap -lubus -lubox -lblobmsg_json -o $(PKG_BUILD_DIR)/doorfast-recorder
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE -DDF_PCM_SUBMIT_PROGRAM -std=c17 -Wall -Wextra -Werror -pedantic \
		-I$(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/src/pcm_submit_main.c $(PKG_BUILD_DIR)/src/gvs_pcm_ingress.c -o $(PKG_BUILD_DIR)/doorfast-pcm-submit
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE -std=c17 -Wall -Wextra -Werror -pedantic -I$(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/src/event_relay.c $(PKG_BUILD_DIR)/event_relay_main.c -lssl -lcrypto -o $(PKG_BUILD_DIR)/doorfast-event-relay
endef

define Package/doorfast/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/doorfast $(1)/usr/sbin/doorfast
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/doorfast-recorder $(1)/usr/sbin/doorfast-recorder
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/doorfast-pcm-submit $(1)/usr/sbin/doorfast-pcm-submit
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/doorfast-event-relay $(1)/usr/sbin/doorfast-event-relay
	$(INSTALL_BIN) ./files/doorfast-site-inventory.sh $(1)/usr/sbin/doorfast-site-inventory
	$(INSTALL_DIR) $(1)/www/cgi-bin
	$(INSTALL_BIN) ./files/doorfast-http.sh $(1)/www/cgi-bin/doorfast
	$(INSTALL_DIR) $(1)/usr/libexec/doorfast
	$(INSTALL_BIN) ./files/doorfast-site-inventory.impl.sh $(1)/usr/libexec/doorfast/doorfast-site-inventory.sh
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/doorfast.init $(1)/etc/init.d/doorfast
	$(INSTALL_BIN) ./files/doorfast-recorder.init $(1)/etc/init.d/doorfast-recorder
	$(INSTALL_BIN) ./files/doorfast-event-relay.init $(1)/etc/init.d/doorfast-event-relay
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/doorfast-group $(1)/etc/uci-defaults/doorfast-group
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/doorfast.config $(1)/etc/config/doorfast
	$(INSTALL_CONF) ./files/doorfast-automation.config $(1)/etc/config/doorfast-automation
	$(INSTALL_CONF) ./files/doorfast-sync.config $(1)/etc/config/doorfast-sync
	$(INSTALL_CONF) ./files/doorfast-deployment.config $(1)/etc/config/doorfast-deployment
	$(INSTALL_CONF) ./files/doorfast-events.config $(1)/etc/config/doorfast-events
	$(INSTALL_DIR) $(1)/etc/doorfast
	chmod 0750 $(1)/etc/doorfast
endef

define Package/doorfast/postinst
#!/bin/sh
if ! group_exists doorfast; then
	group_add_next doorfast >/dev/null
	group_exists doorfast || exit 1
fi
if [ -z "$${IPKG_INSTROOT}" ]; then
	mkdir -p /etc/doorfast || exit 1
	chmod 0750 /etc/doorfast || exit 1
fi
if [ -z "$${IPKG_INSTROOT}" ] && [ "$${PKG_UPGRADE}" = "1" ]; then
	/etc/init.d/doorfast restart
	/etc/init.d/doorfast-event-relay restart
fi
exit 0
endef

$(eval $(call BuildPackage,doorfast))
