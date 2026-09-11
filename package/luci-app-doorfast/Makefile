include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-doorfast
PKG_VERSION:=0.1.0
PKG_RELEASE:=5
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-doorfast
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI support for Doorfast
  DEPENDS:=+doorfast +luci-base +rpcd
endef

define Package/luci-app-doorfast/description
  Doorfast runtime status page and access-controlled local call API.
endef

define Build/Compile
endef

define Package/luci-app-doorfast/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-doorfast.json $(1)/usr/share/luci/menu.d/luci-app-doorfast.json
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-doorfast.json $(1)/usr/share/rpcd/acl.d/luci-app-doorfast.json
	$(INSTALL_DIR) $(1)/www/luci-static/resources/doorfast
	$(INSTALL_DATA) ./htdocs/luci-static/resources/doorfast/status_model.js $(1)/www/luci-static/resources/doorfast/status_model.js
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/doorfast
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/doorfast/status.js $(1)/www/luci-static/resources/view/doorfast/status.js
endef

$(eval $(call BuildPackage,luci-app-doorfast))
