#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
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
static bool  g_imgui_ready   = false;
static bool  g_checkbox_demo = false;
static float g_slider_demo   = 1.0f;
static int   g_frame_count   = 0;

// ── eglSwapBuffers hook ───────────────────────────────────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {

    if (!g_imgui_ready) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        // Ambil ukuran surface langsung
        EGLint w = 0, h = 0;
        eglQuerySurface(dpy, surface, EGL_WIDTH,  &w);
        eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
        if (w <= 0 || h <= 0) { w = 1280; h = 720; }
        io.DisplaySize = ImVec2((float)w, (float)h);

        logff("[GUI] DisplaySize = %dx%d", w, h);

        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

        ImGui::StyleColorsDark();

        bool init_ok = ImGui_ImplAndroidGLES2_Init();
        logff("[GUI] GLES2 init = %s", init_ok ? "OK" : "FAIL");

        // Cek font texture
        ImTextureID tex = io.Fonts->TexID;
        logff("[GUI] font TexID = %u", (unsigned)(uintptr_t)tex);

        g_imgui_ready = true;
        logf("[GUI] ImGui ready");
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
    ImGui::SetNextWindowSize(ImVec2(260, 140), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoCollapse      |
        ImGuiWindowFlags_NoSavedSettings ;

    ImGui::Begin("##amlgui", nullptr, flags);
    ImGui::Text("brruham-arch | AML GUI v1.0");
    ImGui::Separator();
    ImGui::Checkbox("Demo Toggle", &g_checkbox_demo);
    ImGui::SliderFloat("Value", &g_slider_demo, 0.0f, 2.0f);
    ImGui::Text("frame: %d", g_frame_count);
    ImGui::End();

    ImGui::Render();

    ImDrawData* dd = ImGui::GetDrawData();

    // Log debug hanya di frame 1, 2, 3
    if (g_frame_count <= 3) {
        if (!dd) {
            logff("[GUI] frame %d: GetDrawData NULL", g_frame_count);
        } else {
            logff("[GUI] frame %d: CmdListsCount=%d TotalVtx=%d TotalIdx=%d",
                g_frame_count,
                dd->CmdListsCount,
                dd->TotalVtxCount,
                dd->TotalIdxCount);
            logff("[GUI] frame %d: DisplaySize=%.0fx%.0f Valid=%d",
                g_frame_count,
                dd->DisplaySize.x,
                dd->DisplaySize.y,
                dd->Valid);
        }
    }

    if (dd && dd->Valid) {
        ImGui_ImplAndroidGLES2_RenderDrawData(dd);
    }

    return orig_eglSwapBuffers(dpy, surface);
}

// ── AML Entry Points ─────────────────────────────────────────────────────────
extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "amlgui|1.1|ImGui overlay debug|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf("[GUI] OnModPreLoad v1.1");
}

EXPORT void OnModLoad() {
    logf("[GUI] OnModLoad start");

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { logf("[GUI] ERROR: libdobby tidak ditemukan"); return; }

    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) { logf("[GUI] ERROR: DobbyHook sym"); return; }

    void* hEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hEGL) { logf("[GUI] ERROR: libEGL tidak ditemukan"); return; }

    void* addr = dlsym(hEGL, "eglSwapBuffers");
    if (!addr) { logf("[GUI] ERROR: eglSwapBuffers sym"); return; }

    logff("[GUI] eglSwapBuffers addr = %p", addr);

    if (dobbyHook(addr, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers) != 0) {
        logf("[GUI] ERROR: DobbyHook eglSwapBuffers gagal");
        return;
    }

    logf("[GUI] OnModLoad SELESAI — hook terpasang");
}

} // extern "C"
