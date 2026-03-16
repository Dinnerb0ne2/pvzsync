#pragma once
#include <winsock2.h>
#include <windows.h>
#include <GL/GL.h>

#include <imgui.h>
#include <string>
#include <vector>

// 避免重复包含winsock2.h
#ifndef _WINSOCK2API_
#define _WINSOCK2API_
#endif

// 窗口状态
struct WindowState {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
    bool running = true;
};

// 消息类型
enum class MessageType {
    Info,       // 普通信息
    Success,    // 成功信息
    Warning,    // 警告信息
    Error       // 错误信息
};

// 消息项
struct MessageItem {
    std::string text;
    MessageType type;
    float timestamp;  // 消息时间戳
};

// 全局窗口状态
extern WindowState g_window_state;

// 消息系统
extern std::vector<MessageItem> g_messages;
extern const int MAX_MESSAGES;

void AddMessage(const std::string& text, MessageType type = MessageType::Info);
void RenderMessageBar();

// 窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 窗口创建/销毁
bool CreateWin32OpenGLWindow();
void DestroyWin32OpenGLWindow();

// GUI渲染
void RenderGUI();
void UpdateGUIState();