PRODUCT_PACKAGES += \
    vncpasswd \
    VncFlinger

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    packages/services/VncFlinger/sepolicy

PRODUCT_COPY_FILES += \
    packages/services/VncFlinger/etc/VNC-RemoteInput.idc:$(TARGET_COPY_OUT_SYSTEM)/usr/idc/VNC-RemoteInput.idc \
    packages/services/VncFlinger/etc/privapp_whitelist_vncflinger.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/privapp_whitelist_vncflinger.xml \
    packages/services/VncFlinger/etc/default-permissions-vncflinger.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/default-permissions/default-permissions-vncflinger.xml
