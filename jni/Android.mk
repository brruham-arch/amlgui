LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := amlgui

LOCAL_SRC_FILES := \
    ../mod/main.cpp \
    ../mod/imgui_impl_android_gles2.cpp \
    ../mod/imgui/imgui.cpp \
    ../mod/imgui/imgui_draw.cpp \
    ../mod/imgui/imgui_widgets.cpp \
    ../mod/imgui/imgui_tables.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../include \
    $(LOCAL_PATH)/../include/AML \
    $(LOCAL_PATH)/../mod

LOCAL_CPPFLAGS := \
    -std=c++17 \
    -O2 \
    -fPIC \
    -fvisibility=hidden \
    -ffunction-sections \
    -fdata-sections \
    -mthumb \
    -DIMGUI_IMPL_OPENGL_ES2

LOCAL_LDLIBS := \
    -llog \
    -lm \
    -ldl \
    -lEGL \
    -lGLESv2

LOCAL_LDFLAGS := \
    -static-libstdc++ \
    -Wl,--gc-sections

include $(BUILD_SHARED_LIBRARY)
