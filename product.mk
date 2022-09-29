PRODUCT_PACKAGES += \
    vncpasswd \
    VncFlinger

SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += \
    packages/services/VncFlinger/sepolicy

PRODUCT_COPY_FILES += \
    packages/services/VncFlinger/etc/VNC-RemoteInput.idc:$(TARGET_COPY_OUT_SYSTEM)/usr/idc/VNC-RemoteInput.idc

PRODUCT_ARTIFACT_PATH_REQUIREMENT_ALLOWED_LIST += \
    system/usr/idc/VNC-RemoteInput.idc
