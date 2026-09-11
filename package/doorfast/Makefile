include $(TOPDIR)/rules.mk

PKG_NAME:=doorfast
PKG_VERSION:=0.1.0
PKG_RELEASE:=27
PKGARCH:=x86_64
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/doorfast
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Doorfast GVS integration daemon
  DEPENDS:=+libpcap +libuci +libjson-c +libopenssl +libubus +libubox +libblobmsg-json
endef

define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/src
	$(CP) ./src/. $(PKG_BUILD_DIR)/src/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE -DDF_WITH_UBUS -std=c17 -Wall -Wextra -Werror -pedantic \
		-I$(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/src/*.c -lpcap -lubus -lubox -lblobmsg_json -o $(PKG_BUILD_DIR)/doorfast
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE -DDF_WITH_UBUS -DDF_RECORDER_PROGRAM -std=c17 -Wall -Wextra -Werror -pedantic \
		-I$(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/src/*.c -lpcap -lubus -lubox -lblobmsg_json -o $(PKG_BUILD_DIR)/doorfast-recorder
endef

define Package/doorfast/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/doorfast $(1)/usr/sbin/doorfast
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/doorfast-recorder $(1)/usr/sbin/doorfast-recorder
	$(INSTALL_BIN) ./files/doorfast-site-inventory.sh $(1)/usr/sbin/doorfast-site-inventory
	$(INSTALL_DIR) $(1)/usr/libexec/doorfast
	$(INSTALL_BIN) ./files/doorfast-site-inventory.impl.sh $(1)/usr/libexec/doorfast/doorfast-site-inventory.sh
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/doorfast.init $(1)/etc/init.d/doorfast
	$(INSTALL_BIN) ./files/doorfast-recorder.init $(1)/etc/init.d/doorfast-recorder
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/doorfast.config $(1)/etc/config/doorfast
	$(INSTALL_CONF) ./files/doorfast-sync.config $(1)/etc/config/doorfast-sync
	$(INSTALL_CONF) ./files/doorfast-deployment.config $(1)/etc/config/doorfast-deployment
endef

define Package/doorfast/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ] && [ "$${PKG_UPGRADE}" = "1" ]; then
	/etc/init.d/doorfast restart
fi
exit 0
endef

$(eval $(call BuildPackage,doorfast))
