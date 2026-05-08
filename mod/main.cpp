#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <elf.h>
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

// ── Dapatkan base address dari /proc/self/maps ────────────────────────────────
static uintptr_t get_lib_base(const char* libname) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, libname) && strstr(line, "r-xp")) {
            base = (uintptr_t)strtoul(line, nullptr, 16);
            break;
        }
    }
    fclose(f);
    return base;
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

// ── Render ────────────────────────────────────────────────────────────────────
static void do_render(EGLDisplay dpy, EGLSurface surface) {
    EGLContext cur = eglGetCurrentContext();
    if (cur == EGL_NO_CONTEXT) return;

    if (!g_imgui_ready || cur != g_last_context) {
        imgui_shutdown();
        imgui_init(dpy, surface);
    }

    g_frame_count++;

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    ImGuiIO& io = ImGui::GetIO();
    if (w > 0 && h > 0) io.DisplaySize = ImVec2((float)w, (float)h);
    io.WantCaptureMouse    = false;
    io.WantCaptureKeyboard = false;

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
    ImGui::Text("brruham-arch | AML GUI v1.4");
    ImGui::Separator();
    ImGui::Checkbox("Demo Toggle", &g_checkbox_demo);
    ImGui::SliderFloat("Value", &g_slider_demo, 0.0f, 2.0f);
    ImGui::Text("frame=%d reinit=%d", g_frame_count, g_reinit_count);
    ImGui::End();

    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd && dd->Valid) ImGui_ImplAndroidGLES2_RenderDrawData(dd);
}

// ── eglSwapBuffers hook ───────────────────────────────────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    do_render(dpy, surface);
    return orig_eglSwapBuffers(dpy, surface);
}

// ── Inline hook via overwrite fungsi asli (trampoline manual) ─────────────────
// Patch 8 byte pertama fungsi target dengan:
//   LDR PC, [PC, #0]  (Thumb2: F000 F8DF + alamat 32bit)
// Ini lebih permanen dari Dobby karena tidak bisa di-undo oleh dlopen ulang

static uint8_t g_orig_bytes[8] = {0};
static void*   g_target_addr   = nullptr;

static bool inline_hook(void* target, void* hook_fn) {
    uintptr_t t = (uintptr_t)target;
    // Thumb mode — strip bit 0, tapi patch di alamat genap
    bool is_thumb = (t & 1);
    t &= ~1u;

    // Simpan original bytes
    memcpy(g_orig_bytes, (void*)t, 8);

    // Buat page writable
    uintptr_t page = t & ~0xFFFu;
    if (mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        logf("[GUI] inline_hook: mprotect gagal");
        return false;
    }

    if (is_thumb) {
        // Thumb2 absolute jump:
        // LDR.W PC, [PC, #0]  = F8 DF F0 00
        // .word <addr>
        uint8_t patch[8] = {
            0xDF, 0xF8, 0x00, 0xF0,  // LDR.W PC, [PC, #0]
            0x00, 0x00, 0x00, 0x00   // placeholder alamat
        };
        uint32_t dst = (uint32_t)(uintptr_t)hook_fn;
        memcpy(patch + 4, &dst, 4);
        memcpy((void*)t, patch, 8);
    } else {
        // ARM absolute jump:
        // LDR PC, [PC, #-4] = 04 F0 1F E5
        // .word <addr>
        uint8_t patch[8] = {
            0x04, 0xF0, 0x1F, 0xE5,  // LDR PC, [PC, #-4]
            0x00, 0x00, 0x00, 0x00
        };
        uint32_t dst = (uint32_t)(uintptr_t)hook_fn;
        memcpy(patch + 4, &dst, 4);
        memcpy((void*)t, patch, 8);
    }

    // Flush instruction cache
    __builtin___clear_cache((char*)t, (char*)(t + 8));

    mprotect((void*)page, 0x2000, PROT_READ | PROT_EXEC);
    return true;
}

// ── AML Entry Points ─────────────────────────────────────────────────────────
extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "amlgui|1.4|ImGui overlay|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf("[GUI] OnModPreLoad v1.4");
}

EXPORT void OnModLoad() {
    logf("[GUI] OnModLoad start");

    // Dapatkan alamat eglSwapBuffers dari libEGL
    void* hEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hEGL) { logf("[GUI] ERROR: libEGL"); return; }

    void* addr = dlsym(hEGL, "eglSwapBuffers");
    if (!addr) { logf("[GUI] ERROR: eglSwapBuffers sym"); return; }
    logff("[GUI] eglSwapBuffers addr = %p", addr);

    // Simpan original untuk dipanggil kembali
    // Buat trampoline: alokasi executable memory berisi original bytes + jump ke +8
    // Untuk simple: simpan pointer ke fungsi asli sebelum patch
    uintptr_t real = (uintptr_t)addr & ~1u;

    // Alokasi buffer untuk trampoline (original 8 byte + jump kembali)
    void* trampoline = mmap(nullptr, 0x1000,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (trampoline == MAP_FAILED) {
        logf("[GUI] ERROR: mmap trampoline gagal");
        return;
    }

    bool is_thumb = ((uintptr_t)addr & 1);

    // Salin 8 byte original ke trampoline
    memcpy(trampoline, (void*)real, 8);

    // Tambah jump dari trampoline ke (real + 8)
    uintptr_t ret_addr = real + 8;
    if (is_thumb) {
        uint8_t jmp[8] = {
            0xDF, 0xF8, 0x00, 0xF0,
            0x00, 0x00, 0x00, 0x00
        };
        memcpy(jmp + 4, &ret_addr, 4);
        memcpy((uint8_t*)trampoline + 8, jmp, 8);
    } else {
        uint8_t jmp[8] = {
            0x04, 0xF0, 0x1F, 0xE5,
            0x00, 0x00, 0x00, 0x00
        };
        memcpy(jmp + 4, &ret_addr, 4);
        memcpy((uint8_t*)trampoline + 8, jmp, 8);
    }
    __builtin___clear_cache((char*)trampoline, (char*)trampoline + 16);

    // orig_eglSwapBuffers = trampoline (dengan bit Thumb jika perlu)
    uintptr_t tramp_addr = (uintptr_t)trampoline;
    if (is_thumb) tramp_addr |= 1;
    orig_eglSwapBuffers = (eglSwapBuffers_t)tramp_addr;

    logff("[GUI] trampoline = %p (thumb=%d)", trampoline, is_thumb);

    // Pasang inline hook
    if (!inline_hook(addr, (void*)hook_eglSwapBuffers)) {
        logf("[GUI] ERROR: inline_hook gagal");
        return;
    }

    logff("[GUI] inline hook OK, orig=%p", (void*)orig_eglSwapBuffers);
    logf("[GUI] OnModLoad SELESAI");
}

} // extern "C"
