#pragma once
// Backend ImGui minimal untuk Android OpenGL ES 2.0
// Digunakan bersama hook eglSwapBuffers

bool ImGui_ImplAndroidGLES2_Init();
void ImGui_ImplAndroidGLES2_Shutdown();
void ImGui_ImplAndroidGLES2_NewFrame();
void ImGui_ImplAndroidGLES2_RenderDrawData(struct ImDrawData* draw_data);
