LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := uboot_env.c

LOCAL_MODULE := libenv

LOCAL_MODULE_TAGS := eng

LOCAL_C_INCLUDES += vendor/amlogic/frameworks/services/systemcontrol
LOCAL_C_INCLUDES += $(LOCAL_PATH)/..

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_STATIC_LIBRARIES += libsystemcontrol_static liblog libcutils libstdc++ libc libbz
LOCAL_C_INCLUDES += $(LOCAL_PATH)/..

include $(BUILD_STATIC_LIBRARY)