#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_android_gles2.h"

#define LOG_TAG  "libamlgui"
#define LOGFILE  "/storage/emulated/0/amlgui_log.txt"
#define EXPORT   __attribute__((visibility("default")))

// ── Logging ──────────────────────────────────────────────────────────────────
static void logf(const char* msg) {
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", msg);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
static void logff(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    logf(buf);
}

// ── State GUI ────────────────────────────────────────────────────────────────
static bool       g_imgui_ready   = false;
static EGLContext g_last_context  = EGL_NO_CONTEXT;
static bool       g_checkbox_demo = false;
static float      g_slider_demo   = 1.0f;
static int        g_frame_count   = 0;
static int        g_reinit_count  = 0;

// ── ImGui init/shutdown ───────────────────────────────────────────────────────
static void imgui_shutdown() {
    if (!g_imgui_ready) return;
    ImGui_ImplAndroidGLES2_Shutdown();
    ImGui::DestroyContext();
    g_imgui_ready  = false;
    g_last_context = EGL_NO_CONTEXT;
    logf("[GUI] ImGui shutdown");
}

static void imgui_init(EGLDisplay dpy, EGLSurface surface) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) { w = 2262; h = 1080; }
    io.DisplaySize = ImVec2((float)w, (float)h);

    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

    ImGui::StyleColorsDark();
    ImGui_ImplAndroidGLES2_Init();

    g_imgui_ready  = true;
    g_last_context = eglGetCurrentContext();

    logff("[GUI] ImGui init #%d ctx=%p size=%dx%d",
        ++g_reinit_count, (void*)g_last_context, w, h);
}

// ── eglSwapBuffers hook ───────────────────────────────────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {

    EGLContext cur = eglGetCurrentContext();

    // Context berubah atau belum init — reinit ImGui
    if (!g_imgui_ready || cur != g_last_context) {
        if (cur == EGL_NO_CONTEXT) {
            // Context tidak valid, skip frame ini
            return orig_eglSwapBuffers(dpy, surface);
        }
        imgui_shutdown();
        imgui_init(dpy, surface);
    }

    g_frame_count++;

    // Update display size
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    ImGuiIO& io = ImGui::GetIO();
    if (w > 0 && h > 0) io.DisplaySize = ImVec2((float)w, (float)h);

    io.WantCaptureMouse    = false;
    io.WantCaptureKeyboard = false;

    // ── Render ──────────────────────────────────────────────────────────────
    ImGui_ImplAndroidGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280, 130), ImGuiCond_Always);

    ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoCollapse      |
        ImGuiWindowFlags_NoSavedSettings ;

    ImGui::Begin("##amlgui", nullptr, wf);
    ImGui::Text("brruham-arch | AML GUI v1.2");
    ImGui::Separator();
    ImGui::Checkbox("Demo Toggle", &g_checkbox_demo);
    ImGui::SliderFloat("Value", &g_slider_demo, 0.0f, 2.0f);
    ImGui::Text("frame=%d reinit=%d", g_frame_count, g_reinit_count);
    ImGui::End();

    ImGui::Render();

    ImDrawData* dd = ImGui::GetDrawData();
    if (dd && dd->Valid) {
        ImGui_ImplAndroidGLES2_RenderDrawData(dd);
    }

    return orig_eglSwapBuffers(dpy, surface);
}

// ── AML Entry Points ─────────────────────────────────────────────────────────
extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "amlgui|1.2|ImGui overlay|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf("[GUI] OnModPreLoad v1.2");
}

EXPORT void OnModLoad() {
    logf("[GUI] OnModLoad start");

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { logf("[GUI] ERROR: libdobby"); return; }

    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) { logf("[GUI] ERROR: DobbyHook sym"); return; }

    void* hEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hEGL) { logf("[GUI] ERROR: libEGL"); return; }

    void* addr = dlsym(hEGL, "eglSwapBuffers");
    if (!addr) { logf("[GUI] ERROR: eglSwapBuffers sym"); return; }

    logff("[GUI] eglSwapBuffers addr = %p", addr);

    if (dobbyHook(addr, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers) != 0) {
        logf("[GUI] ERROR: DobbyHook gagal");
        return;
    }

    logf("[GUI] OnModLoad SELESAI");
}

} // extern "C"
