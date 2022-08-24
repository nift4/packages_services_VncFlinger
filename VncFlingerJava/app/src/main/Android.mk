LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PACKAGE_NAME := VncFlingerJava
LOCAL_PRIVATE_PLATFORM_APIS := true
LOCAL_USE_AAPT2 := true
LOCAL_CERTIFICATE := platform
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(call all-java-files-under, java)
LOCAL_PROGUARD_FLAG_FILES := ../../proguard-rules.pro
LOCAL_JNI_SHARED_LIBRARIES := libvncflinger
LOCAL_REQUIRED_MODULES := libvncflinger
include $(BUILD_PACKAGE)

include $(LOCAL_PATH)/cpp/Android.mk
