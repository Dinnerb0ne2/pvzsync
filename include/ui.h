#pragma once
#include <winsock2.h>
#include <windows.h>
#include <GL/GL.h>

#include <imgui.h>

// 窗口状态
struct WindowState {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
    bool running = true;
};

// 全局窗口状态
extern WindowState g_window_state;

// 窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 窗口创建/销毁
bool CreateWin32OpenGLWindow();
void DestroyWin32OpenGLWindow();

// GUI渲染
void RenderGUI();
void UpdateGUIState();