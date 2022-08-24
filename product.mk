SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    external/vncflinger/sepolicy

PRODUCT_PACKAGES += \
    vncflinger \
    vncpasswd \
    VncFlingerJava

PRODUCT_COPY_FILES += \
    external/vncflinger/etc/VNC-RemoteInput.idc:$(TARGET_COPY_OUT_SYSTEM)/usr/idc/VNC-RemoteInput.idc
