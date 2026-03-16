#include "ui.h"
#include "core.h"
#include "network.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include <string.h>
#include <iostream>
#include <shlwapi.h>

// 前向声明ImGui_ImplWin32_WndProcHandler（从imgui_impl_win32.h复制）
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 全局窗口状态
WindowState g_window_state;

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_CLOSE:
        g_window_state.running = false;
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

// 创建Win32+OpenGL窗口
bool CreateWin32OpenGLWindow() {
    // 注册窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = "PVZSyncClass";

    if (!RegisterClassExA(&wc)) {
        std::cerr << "窗口类注册失败: " << GetLastError() << std::endl;
        return false;
    }

    // 创建窗口
    g_window_state.hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "PVZ内网联动工具",
        WS_OVERLAPPEDWINDOW,
        100, 100, 900, 700,
        NULL, NULL, wc.hInstance, NULL
    );

    if (!g_window_state.hwnd) {
        std::cerr << "窗口创建失败: " << GetLastError() << std::endl;
        return false;
    }

    // 初始化OpenGL
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize      = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    g_window_state.hdc = GetDC(g_window_state.hwnd);
    int pf = ChoosePixelFormat(g_window_state.hdc, &pfd);
    SetPixelFormat(g_window_state.hdc, pf, &pfd);

    g_window_state.hglrc = wglCreateContext(g_window_state.hdc);
    wglMakeCurrent(g_window_state.hdc, g_window_state.hglrc);

    // 显示窗口
    ShowWindow(g_window_state.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_window_state.hwnd);

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_window_state.hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 加载中文字体（微软雅黑）
    const char* font_path = "C:\\Windows\\Fonts\\msyh.ttf";
    ImFontConfig font_config;
    font_config.OversampleH = 2;
    font_config.OversampleV = 2;
    font_config.PixelSnapH = false;
    
    // 检查字体文件是否存在
    if (GetFileAttributesA(font_path) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(font_path, 16.0f, &font_config, io.Fonts->GetGlyphRangesChineseFull());
    } else {
        // 如果微软雅黑不存在，尝试使用宋体
        const char* font_path_fallback = "C:\\Windows\\Fonts\\simsun.ttc";
        if (GetFileAttributesA(font_path_fallback) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(font_path_fallback, 16.0f, &font_config, io.Fonts->GetGlyphRangesChineseFull());
        }
    }

    return true;
}

// 销毁窗口
void DestroyWin32OpenGLWindow() {
    // 清理ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // 清理OpenGL
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(g_window_state.hglrc);
    ReleaseDC(g_window_state.hwnd, g_window_state.hdc);

    // 销毁窗口
    DestroyWindow(g_window_state.hwnd);
    UnregisterClassA("PVZSyncClass", GetModuleHandle(NULL));
}

// 更新GUI状态
void UpdateGUIState() {
    CheckPVZRunning();
}

// 渲染GUI
void RenderGUI() {
    // 开始ImGui帧
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // 设置窗口
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(850, 650));
    ImGui::Begin("PVZ 内网联动工具", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // 基础配置
    if (ImGui::CollapsingHeader("基础配置 [General]", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("随动打开PVZ", &g_config.allow_auto_open);
        
        char role_buf[32] = {0};
        strncpy(role_buf, g_config.role.c_str(), sizeof(role_buf)-1);
        ImGui::InputText("本机角色 (server/client)", role_buf, sizeof(role_buf));
        g_config.role = role_buf;

        char peer_ip_buf[32] = {0};
        strncpy(peer_ip_buf, g_config.peer_ip.c_str(), sizeof(peer_ip_buf)-1);
        ImGui::InputText("对方IP", peer_ip_buf, sizeof(peer_ip_buf));
        g_config.peer_ip = peer_ip_buf;

        ImGui::InputInt("通信端口", &g_config.peer_port);
    }

    // 路径配置
    if (ImGui::CollapsingHeader("路径配置", ImGuiTreeNodeFlags_DefaultOpen)) {
        char pvz_path_buf[512] = {0};
        strncpy(pvz_path_buf, g_config.pvz_path.c_str(), sizeof(pvz_path_buf)-1);
        ImGui::InputText("PVZ程序路径", pvz_path_buf, sizeof(pvz_path_buf));
        g_config.pvz_path = pvz_path_buf;

        char save_path_buf[512] = {0};
        strncpy(save_path_buf, g_config.save_path.c_str(), sizeof(save_path_buf)-1);
        ImGui::InputText("存档文件夹路径", save_path_buf, sizeof(save_path_buf));
        g_config.save_path = save_path_buf;

        char local_backup_buf[512] = {0};
        strncpy(local_backup_buf, g_config.local_backup_path.c_str(), sizeof(local_backup_buf)-1);
        ImGui::InputText("本地备份路径", local_backup_buf, sizeof(local_backup_buf));
        g_config.local_backup_path = local_backup_buf;

        char remote_backup_buf[512] = {0};
        strncpy(remote_backup_buf, g_config.remote_backup_path.c_str(), sizeof(remote_backup_buf)-1);
        ImGui::InputText("远程备份路径", remote_backup_buf, sizeof(remote_backup_buf));
        g_config.remote_backup_path = remote_backup_buf;
    }

    // 运行状态
    if (ImGui::CollapsingHeader("运行状态", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("连接状态: %s", g_network_state.connected ? "✅ 已连接" : "❌ 未连接");
        ImGui::Text("PVZ状态: %s", g_pvz_state.is_running ? "▶️ 运行中" : "⏹️ 未运行");
        
        if (PathIsDirectoryA(g_config.save_path.c_str())) {
            time_t latest_time = GetLatestFileTimeInDir(g_config.save_path);
            ImGui::Text("存档最新修改时间: %s", latest_time > 0 ? ctime(&latest_time) : "未知");
        }
    }

    // 操作按钮
    ImGui::Spacing();
    if (ImGui::Button("保存配置到INI", ImVec2(120, 30))) {
        SaveConfig("config.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button("从INI加载配置", ImVec2(120, 30))) {
        ReadConfig("config.ini");
    }

    ImGui::SameLine();
    if (ImGui::Button("连接对方", ImVec2(100, 30)) && !g_network_state.connected) {
        if (g_config.role == "server") {
            StartServer(g_config.peer_port);
        } else {
            ConnectToServer(g_config.peer_ip, g_config.peer_port);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("启动PVZ", ImVec2(100, 30))) {
        StartPVZ(g_config.pvz_path);
        if (g_network_state.connected) {
            SendCommand("START_PVZ");
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("备份存档到本地", ImVec2(150, 30))) {
        BackupSaveDir(g_config.save_path, g_config.local_backup_path);
    }
    ImGui::SameLine();
    if (ImGui::Button("发送远程备份指令", ImVec2(150, 30)) && g_network_state.connected) {
        SendCommand("BACKUP_REMOTE");
    }

    // 结束GUI
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SwapBuffers(g_window_state.hdc);
}