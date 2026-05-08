// imgui_impl_android_gles2.cpp
// Backend ImGui minimal untuk Android OpenGL ES 2.0
// Hanya render — input touch bisa ditambah nanti

#include "imgui/imgui.h"
#include "imgui/imgui_impl_android_gles2.h"
#include <GLES2/gl2.h>
#include <string.h>
#include <stdlib.h>
#include <android/log.h>

#define LOG_TAG "libamlgui"

// ── Shader source ─────────────────────────────────────────────────────────────

static const char* g_vert_src = R"(
    uniform mat4 u_ProjMtx;
    attribute vec2 a_Position;
    attribute vec2 a_UV;
    attribute vec4 a_Color;
    varying vec2 v_UV;
    varying vec4 v_Color;
    void main() {
        v_UV    = a_UV;
        v_Color = a_Color;
        gl_Position = u_ProjMtx * vec4(a_Position, 0.0, 1.0);
    }
)";

static const char* g_frag_src = R"(
    precision mediump float;
    uniform sampler2D u_Texture;
    varying vec2 v_UV;
    varying vec4 v_Color;
    void main() {
        gl_FragColor = v_Color * texture2D(u_Texture, v_UV);
    }
)";

// ── GL state ──────────────────────────────────────────────────────────────────

static GLuint g_prog      = 0;
static GLuint g_font_tex  = 0;
static GLint  g_loc_proj  = -1;
static GLint  g_loc_tex   = -1;
static GLint  g_loc_pos   = -1;
static GLint  g_loc_uv    = -1;
static GLint  g_loc_col   = -1;

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "shader err: %s", buf);
    }
    return s;
}

bool ImGui_ImplAndroidGLES2_Init() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   g_vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, g_frag_src);

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    g_loc_proj = glGetUniformLocation(g_prog, "u_ProjMtx");
    g_loc_tex  = glGetUniformLocation(g_prog, "u_Texture");
    g_loc_pos  = glGetAttribLocation (g_prog, "a_Position");
    g_loc_uv   = glGetAttribLocation (g_prog, "a_UV");
    g_loc_col  = glGetAttribLocation (g_prog, "a_Color");

    // Upload font texture
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    glGenTextures(1, &g_font_tex);
    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    io.Fonts->SetTexID((ImTextureID)(uintptr_t)g_font_tex);

    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG,
        "[GLES2] init ok prog=%u font_tex=%u", g_prog, g_font_tex);
    return true;
}

void ImGui_ImplAndroidGLES2_Shutdown() {
    if (g_font_tex) { glDeleteTextures(1, &g_font_tex); g_font_tex = 0; }
    if (g_prog)     { glDeleteProgram(g_prog);           g_prog = 0; }
}

void ImGui_ImplAndroidGLES2_NewFrame() {
    // Tidak perlu lakukan apa-apa — display size sudah di-set di main.cpp
}

void ImGui_ImplAndroidGLES2_RenderDrawData(ImDrawData* draw_data) {
    if (!draw_data || draw_data->CmdListsCount == 0) return;

    int fb_w = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_h = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_w <= 0 || fb_h <= 0) return;

    // Simpan GL state
    GLint last_program, last_texture, last_blend_src, last_blend_dst;
    GLint last_viewport[4];
    GLboolean last_blend, last_cull, last_depth;
    glGetIntegerv(GL_CURRENT_PROGRAM,    &last_program);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,    &last_blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA,    &last_blend_dst);
    glGetIntegerv(GL_VIEWPORT,           last_viewport);
    last_blend = glIsEnabled(GL_BLEND);
    last_cull  = glIsEnabled(GL_CULL_FACE);
    last_depth = glIsEnabled(GL_DEPTH_TEST);

    // Set state untuk ImGui
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, fb_w, fb_h);

    // Projection matrix orthographic
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float proj[4][4] = {
        { 2.0f/(R-L),    0.0f,          0.0f, 0.0f },
        { 0.0f,          2.0f/(T-B),    0.0f, 0.0f },
        { 0.0f,          0.0f,         -1.0f, 0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),   0.0f, 1.0f },
    };

    glUseProgram(g_prog);
    glUniform1i(g_loc_tex, 0);
    glUniformMatrix4fv(g_loc_proj, 1, GL_FALSE, &proj[0][0]);

    ImVec2 clip_off   = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buf  = cmd_list->VtxBuffer.Data;
        const ImDrawIdx*  idx_buf  = cmd_list->IdxBuffer.Data;

        glVertexAttribPointer(g_loc_pos, 2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert),
            (const void*)((char*)vtx_buf + offsetof(ImDrawVert, pos)));
        glVertexAttribPointer(g_loc_uv,  2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert),
            (const void*)((char*)vtx_buf + offsetof(ImDrawVert, uv)));
        glVertexAttribPointer(g_loc_col, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(ImDrawVert),
            (const void*)((char*)vtx_buf + offsetof(ImDrawVert, col)));
        glEnableVertexAttribArray(g_loc_pos);
        glEnableVertexAttribArray(g_loc_uv);
        glEnableVertexAttribArray(g_loc_col);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
                continue;
            }
            ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                            (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
            ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                            (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;

            glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)pcmd->GetTexID());
            glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount,
                sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                idx_buf + pcmd->IdxOffset);
        }
        idx_buf += cmd_list->IdxBuffer.Size;
    }

    // Restore GL state
    glUseProgram(last_program);
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBlendFunc(last_blend_src, last_blend_dst);
    if (!last_blend)  glDisable(GL_BLEND);
    if (last_cull)    glEnable(GL_CULL_FACE);
    if (last_depth)   glEnable(GL_DEPTH_TEST);
    glViewport(last_viewport[0], last_viewport[1],
               last_viewport[2], last_viewport[3]);
}
