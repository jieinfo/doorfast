include $(TOPDIR)/rules.mk

PKG_NAME:=doorfast
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKGARCH:=x86_64
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/doorfast
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Doorfast GVS integration daemon
  DEPENDS:=+libpcap +libuci +libjson-c +libopenssl
endef

define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/src
	$(CP) ./src/. $(PKG_BUILD_DIR)/src/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE -std=c17 -Wall -Wextra -Werror -pedantic \
		-I$(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/src/*.c -lpcap -o $(PKG_BUILD_DIR)/doorfast
endef

define Package/doorfast/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/doorfast $(1)/usr/sbin/doorfast
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/doorfast.init $(1)/etc/init.d/doorfast
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/doorfast.config $(1)/etc/config/doorfast
	$(INSTALL_CONF) ./files/doorfast-sync.config $(1)/etc/config/doorfast-sync
endef

$(eval $(call BuildPackage,doorfast))
