#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

// ── ImGui ────────────────────────────────────────────────────────────────────
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
static bool  g_gui_visible   = true;   // toggle via sentuh area pojok kiri atas
static bool  g_imgui_ready   = false;
static bool  g_checkbox_demo = false;
static float g_slider_demo   = 1.0f;

// ── eglSwapBuffers hook ───────────────────────────────────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {

    if (!g_imgui_ready) {
        // Init ImGui sekali saat frame pertama
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 720); // fallback — akan diupdate tiap frame

        ImGui::StyleColorsDark();
        ImGui_ImplAndroidGLES2_Init();

        g_imgui_ready = true;
        logf("[GUI] ImGui ready");
    }

    // Update display size dari EGL surface
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    ImGuiIO& io = ImGui::GetIO();
    if (w > 0 && h > 0) {
        io.DisplaySize = ImVec2((float)w, (float)h);
    }

    // ── Render frame ImGui ──────────────────────────────────────────────────
    ImGui_ImplAndroidGLES2_NewFrame();
    ImGui::NewFrame();

    if (g_gui_visible) {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(260, 160), ImGuiCond_Once);

        ImGui::Begin("AML GUI", &g_gui_visible);
        ImGui::Text("brruham-arch | AML Mod");
        ImGui::Separator();

        ImGui::Checkbox("Demo Toggle", &g_checkbox_demo);
        ImGui::SliderFloat("Value", &g_slider_demo, 0.0f, 2.0f);

        if (ImGui::Button("Close")) {
            g_gui_visible = false;
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplAndroidGLES2_RenderDrawData(ImGui::GetDrawData());

    // Panggil aslinya
    return orig_eglSwapBuffers(dpy, surface);
}

// ── Touch passthrough — tap pojok kiri atas untuk toggle GUI ─────────────────
// (opsional: bisa dikembangkan nanti via ANativeActivity touch hook)

// ── AML Entry Points ─────────────────────────────────────────────────────────
extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "amlgui|1.0|ImGui overlay demo|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf("[GUI] OnModPreLoad v1.0");
}

EXPORT void OnModLoad() {
    logf("[GUI] OnModLoad start");

    // Dobby untuk hook eglSwapBuffers
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { logf("[GUI] ERROR: libdobby tidak ditemukan"); return; }

    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) { logf("[GUI] ERROR: DobbyHook sym"); return; }

    // Ambil alamat eglSwapBuffers dari libEGL.so
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
