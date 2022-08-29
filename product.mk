PRODUCT_PACKAGES += \
    vncpasswd \
    VncFlinger

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    external/vncflinger/sepolicy

PRODUCT_COPY_FILES += \
    external/vncflinger/etc/VNC-RemoteInput.idc:$(TARGET_COPY_OUT_SYSTEM)/usr/idc/VNC-RemoteInput.idc \
    external/vncflinger/etc/privapp_whitelist_vncflinger.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp_whitelist_vncflinger.xml \
    external/vncflinger/etc/default-permissions-vncflinger.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/default-permissions/default-permissions-vncflinger.xml
