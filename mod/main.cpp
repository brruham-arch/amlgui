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

// ── Dapatkan base address library dari /proc/self/maps ────────────────────────
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
    ImGui::Text("brruham-arch | AML GUI v1.3");
    ImGui::Separator();
    ImGui::Checkbox("Demo Toggle", &g_checkbox_demo);
    ImGui::SliderFloat("Value", &g_slider_demo, 0.0f, 2.0f);
    ImGui::Text("frame=%d reinit=%d", g_frame_count, g_reinit_count);
    ImGui::End();

    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd && dd->Valid) ImGui_ImplAndroidGLES2_RenderDrawData(dd);
}

// ── GOT patch ─────────────────────────────────────────────────────────────────
static bool got_patch(uintptr_t base, const char* sym_name, void* new_fn, void** old_fn) {
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)base;
    if (ehdr->e_ident[0] != 0x7f) { logf("[GUI] GOT: bukan ELF"); return false; }

    Elf32_Phdr* phdr = (Elf32_Phdr*)(base + ehdr->e_phoff);
    Elf32_Dyn*  dyn  = nullptr;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn = (Elf32_Dyn*)(base + phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) { logf("[GUI] GOT: PT_DYNAMIC tidak ada"); return false; }

    Elf32_Sym*  symtab  = nullptr;
    char*       strtab  = nullptr;
    Elf32_Rel*  plt_rel = nullptr;
    Elf32_Word  plt_sz  = 0;

    for (Elf32_Dyn* d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB:   symtab  = (Elf32_Sym*)(base + d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab  = (char*)     (base + d->d_un.d_ptr); break;
            case DT_JMPREL:   plt_rel = (Elf32_Rel*)(base + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: plt_sz  = d->d_un.d_val;                      break;
        }
    }

    if (!symtab || !strtab || !plt_rel) {
        logf("[GUI] GOT: tabel tidak lengkap"); return false;
    }

    int count = (int)(plt_sz / sizeof(Elf32_Rel));
    logff("[GUI] GOT: scan %d PLT entries di base=0x%08x", count, (unsigned)base);

    for (int i = 0; i < count; i++) {
        int sym_idx  = ELF32_R_SYM(plt_rel[i].r_info);
        const char* name = strtab + symtab[sym_idx].st_name;
        if (strcmp(name, sym_name) == 0) {
            void** got_entry = (void**)(base + plt_rel[i].r_offset);
            logff("[GUI] GOT: found %s offset=0x%x cur=%p", name, plt_rel[i].r_offset, *got_entry);
            *old_fn = *got_entry;
            uintptr_t page = (uintptr_t)got_entry & ~0xFFFu;
            mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE);
            *got_entry = new_fn;
            mprotect((void*)page, 0x1000, PROT_READ);
            logff("[GUI] GOT: patched -> %p", new_fn);
            return true;
        }
    }
    logff("[GUI] GOT: %s tidak ditemukan", sym_name);
    return false;
}

// ── eglSwapBuffers hook ───────────────────────────────────────────────────────
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    do_render(dpy, surface);
    return orig_eglSwapBuffers(dpy, surface);
}

// ── AML Entry Points ─────────────────────────────────────────────────────────
extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "amlgui|1.3|ImGui overlay|brruham";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf("[GUI] OnModPreLoad v1.3");
}

EXPORT void OnModLoad() {
    logf("[GUI] OnModLoad start");

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { logf("[GUI] ERROR: libdobby"); return; }

    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) { logf("[GUI] ERROR: DobbyHook sym"); return; }

    // ── Hook Dobby di libEGL ──────────────────────────────────────────────
    void* hEGL = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hEGL) { logf("[GUI] ERROR: libEGL"); return; }

    void* addr = dlsym(hEGL, "eglSwapBuffers");
    if (!addr) { logf("[GUI] ERROR: eglSwapBuffers sym"); return; }
    logff("[GUI] eglSwapBuffers addr = %p", addr);

    if (dobbyHook(addr, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers) != 0) {
        logf("[GUI] ERROR: DobbyHook gagal"); return;
    }
    logf("[GUI] Dobby hook OK");

    // ── GOT patch di libGTASA.so ─────────────────────────────────────────
    uintptr_t gtasa_base = get_lib_base("libGTASA.so");
    if (!gtasa_base) {
        logf("[GUI] WARN: libGTASA base tidak ditemukan, skip GOT patch");
    } else {
        logff("[GUI] libGTASA base = 0x%08x", (unsigned)gtasa_base);
        void* old = nullptr;
        bool ok = got_patch(gtasa_base, "eglSwapBuffers",
                            (void*)hook_eglSwapBuffers, &old);
        // orig sudah di-set oleh Dobby, tidak perlu override dari GOT
        logff("[GUI] GOT patch libGTASA = %s", ok ? "OK" : "FAIL");
    }

    logf("[GUI] OnModLoad SELESAI");
}

} // extern "C"
