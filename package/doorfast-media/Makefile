include $(TOPDIR)/rules.mk

PKG_NAME:=doorfast-media
PKG_VERSION:=0.1.0
PKG_RELEASE:=54
PKGARCH:=x86_64
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

DF_MEDIA_SOURCES:= \
	$(PKG_BUILD_DIR)/src/gvs_video_reassembly.c \
	$(PKG_BUILD_DIR)/src/gvs_monitor.c \
	$(PKG_BUILD_DIR)/src/gvs_station.c \
	$(PKG_BUILD_DIR)/src/media_capacity.c \
	$(PKG_BUILD_DIR)/src/media_credentials.c \
	$(PKG_BUILD_DIR)/src/media_encoder.c \
	$(PKG_BUILD_DIR)/src/media_frame_queue.c \
	$(PKG_BUILD_DIR)/src/media_module_config.c \
	$(PKG_BUILD_DIR)/src/media_session.c \
	$(PKG_BUILD_DIR)/src/media_session_manager.c

define Package/doorfast-media
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Doorfast optional H.264 preview media module
  DEPENDS:=+doorfast +ffmpeg +libffmpeg-full +libx264
endef

define Package/doorfast-media/description
  Optional Doorfast preview module. It loads into the Doorfast daemon and does
  not install a listener or a separate init service.
endef

define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)/src
	$(CP) ./src/. $(PKG_BUILD_DIR)/src/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE \
		-std=c17 -Wall -Wextra -Werror -pedantic -fPIC -shared \
		-Wl,-z,defs -I$(PKG_BUILD_DIR)/src $(DF_MEDIA_SOURCES) \
		-o $(PKG_BUILD_DIR)/media-v3.so
endef

define Package/doorfast-media/install
	$(INSTALL_DIR) $(1)/usr/lib/doorfast
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/media-v3.so $(1)/usr/lib/doorfast/media-v3.so
endef

define Package/doorfast-media/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ] && [ "$${PKG_UPGRADE}" = "1" ]; then
	test -r /usr/lib/doorfast/media-v3.so || exit 1
	/etc/init.d/doorfast restart
fi
exit 0
endef

$(eval $(call BuildPackage,doorfast-media))
