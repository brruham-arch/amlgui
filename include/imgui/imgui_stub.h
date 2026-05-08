#pragma once
// ImGui minimal declarations — implementasi di imgui_impl.cpp
// Full source: https://github.com/ocornut/imgui (copy imgui.h, imgui.cpp,
// imgui_draw.cpp, imgui_widgets.cpp, imgui_tables.cpp ke folder ini)

#include <stdint.h>
#include <stdarg.h>

struct ImVec2 { float x, y; ImVec2(float _x=0,float _y=0):x(_x),y(_y){} };
struct ImVec4 { float x,y,z,w; ImVec4(float _x=0,float _y=0,float _z=0,float _w=0):x(_x),y(_y),z(_z),w(_w){} };

typedef unsigned int ImGuiWindowFlags;
typedef unsigned int ImGuiID;

enum ImGuiWindowFlags_ {
    ImGuiWindowFlags_None             = 0,
    ImGuiWindowFlags_NoTitleBar       = 1 << 0,
    ImGuiWindowFlags_NoResize         = 1 << 1,
    ImGuiWindowFlags_NoMove           = 1 << 2,
    ImGuiWindowFlags_NoScrollbar      = 1 << 3,
    ImGuiWindowFlags_AlwaysAutoResize = 1 << 6,
};

namespace ImGui {
    bool Begin(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);
    void End();
    void Text(const char* fmt, ...);
    bool Button(const char* label, ImVec2 size = ImVec2(0,0));
    bool Checkbox(const char* label, bool* v);
    void SliderFloat(const char* label, float* v, float v_min, float v_max);
    void Separator();
    void SetNextWindowPos(ImVec2 pos);
    void SetNextWindowSize(ImVec2 size);
    void NewFrame();
    void Render();
}
