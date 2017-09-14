LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    RvcTrack.cpp \
    RvcCamera.cpp \
    RvcSocket.cpp \
    native_rvc_main.cpp

LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CFLAGS += -Wno-unused-parameter

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    liblog \
    libandroidfw \
    libutils \
    libbinder \
    libui \
    libskia \
    libEGL \
    libGLESv1_CM \
    libgui \
    libtinyalsa \
    libcamera_client \
    libcamera_metadata


LOCAL_MODULE:= nativervc

ifdef TARGET_32_BIT_SURFACEFLINGER
LOCAL_32_BIT_ONLY := true
endif

include $(BUILD_EXECUTABLE)
