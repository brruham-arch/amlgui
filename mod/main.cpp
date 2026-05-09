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

static uintptr_t get_lib_base(const char* libname) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, libname) && strstr(line, "r-xp")) {
            unsigned long addr = 0;
            sscanf(line, "%lx", &addr);
            base = (uintptr_t)addr;
            break;
        }
    }
    fclose(f);
    return base;
}

static bool       g_imgui_ready  = false;
static EGLContext g_last_context = EGL_NO_CONTEXT;
static bool       g_checkbox     = false;
static float      g_slider       = 1.0f;
static int        g_frame        = 0;
static int        g_reinit       = 0;

static void imgui_shutdown() {
    if (!g_imgui_ready) return;
    ImGui_ImplAndroidGLES2_Shutdown();
    ImGui::DestroyContext();
    g_imgui_ready  = false;
    g_last_context = EGL_NO_CONTEXT;
}

static void imgui_init() {
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLSurface srf = eglGetCurrentSurface(EGL_DRAW);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    EGLint w = 0, h = 0;
    if (dpy && srf) {
        eglQuerySurface(dpy, srf, EGL_WIDTH,  &w);
        eglQuerySurface(dpy, srf, EGL_HEIGHT, &h);
    }
    if (w <= 0 || h <= 0) { w = 2262; h = 1080; }
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    ImGui::StyleColorsDark();
    ImGui_ImplAndroidGLES2_Init();
    g_imgui_ready  = true;
    g_last_context = eglGetCurrentContext();
    logff("[GUI] init #%d ctx=%p size=%dx%d", ++g_reinit, (void*)g_last_context, w, h);
}

static void do_render() {
    EGLContext cur = eglGetCurrentContext();
    if (cur == EGL_NO_CONTEXT) return;
    if (!g_imgui_ready || cur != g_last_context) {
        imgui_shutdown();
        imgui_init();
    }
    g_frame++;

    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLSurface srf = eglGetCurrentSurface(EGL_DRAW);
    EGLint w = 0, h = 0;
    if (dpy && srf) {
        eglQuerySurface(dpy, srf, EGL_WIDTH,  &w);
        eglQuerySurface(dpy, srf, EGL_HEIGHT, &h);
    }
    ImGuiIO& io = ImGui::GetIO();
    if (w > 0 && h > 0) io.DisplaySize = ImVec2((float)w, (float)h);
    io.WantCaptureMouse    = false;
    io.WantCaptureKeyboard = false;

    ImGui_ImplAndroidGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(280, 130), ImGuiCond_Always);
    ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##amlgui", nullptr, wf);
    ImGui::Text("brruham-arch | AML GUI v2.1");
    ImGui::Separator();
    ImGui::Checkbox("Demo", &g_checkbox);
    ImGui::SliderFloat("Value", &g_slider, 0.0f, 2.0f);
    ImGui::Text("frame=%d reinit=%d", g_frame, g_reinit);
    ImGui::End();

    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd && dd->Valid) ImGui_ImplAndroidGLES2_RenderDrawData(dd);
}

typedef void (*DoGameState_t)(void);
static DoGameState_t orig_DoGameState = nullptr;

static void hook_DoGameState(void) {
    orig_DoGameState();
    do_render();
}

extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "amlgui|2.1|ImGui overlay|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf("[GUI] OnModPreLoad v2.1");
}

EXPORT void OnModLoad() {
    logf("[GUI] OnModLoad start");

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { logf("[GUI] ERROR: libdobby"); return; }
    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) { logf("[GUI] ERROR: DobbyHook sym"); return; }

    // Pakai dlsym seperti AML_ImGui
    void* hGTASA = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!hGTASA) { logf("[GUI] ERROR: libGTASA handle"); return; }

    void* target = dlsym(hGTASA, "_Z11DoGameStatev");
    if (!target) { logf("[GUI] ERROR: DoGameState sym"); return; }
    logff("[GUI] DoGameState = %p", target);

    if (dobbyHook(target, (void*)hook_DoGameState,
                  (void**)&orig_DoGameState) != 0) {
        logf("[GUI] ERROR: DobbyHook gagal");
        return;
    }

    logf("[GUI] hook DoGameState OK");
    logf("[GUI] OnModLoad SELESAI");
}

} // extern "C"
