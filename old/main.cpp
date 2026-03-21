#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <ctime>
#include <winsock2.h>
#include <windows.h>
#include <wingdi.h>
#include <shlwapi.h>
#include <direct.h>

// ImGui相关 - 使用Win32后端
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_opengl3.h"

// OpenGL头文件 - Windows自带
#include <GL/gl.h>

// 定义GL_BGRA（Windows扩展）
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

// 链接必要的库
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "shlwapi.lib")

// 全局配置（严格对应INI的[General]段）
struct Config {
    bool allow_auto_open = true;          // 随动打开开关
    std::string role = "server";          // 角色: server/client
    std::string peer_ip = "192.168.1.105";// 对方IP
    int peer_port = 8888;                 // 通信端口
    std::string pvz_path = "C:\\Program Files\\PlantsVsZombies\\PlantsVsZombies.exe";
    std::string save_path = "C:\\Users\\你的用户名\\AppData\\Roaming\\PopCap Games\\PlantsVsZombies\\userdata";
    std::string local_backup_path = "C:\\PVZBackup\\Local";
    std::string remote_backup_path = "C:\\PVZBackup\\Remote";
} g_config;

// 全局状态
struct State {
    SOCKET sock = INVALID_SOCKET;
    bool connected = false;
    bool pvz_running = false;
    unsigned char* screen_data = nullptr;
    int screen_width = 0;
    int screen_height = 0;
    std::mutex mtx;
} g_state;

// Win32全局变量
HWND g_hwnd = NULL;
HDC g_hDC = NULL;
HGLRC g_hRC = NULL;
bool g_done = false;

// 前向声明
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ===================== 配置读写 =====================
void ReadConfig(const std::string& path) {
    char buf[512];
    GetPrivateProfileStringA("General", "AllowAutoOpen", "1", buf, 512, path.c_str());
    g_config.allow_auto_open = (atoi(buf) == 1);

    GetPrivateProfileStringA("General", "Role", "server", buf, 512, path.c_str());
    g_config.role = buf;

    GetPrivateProfileStringA("General", "PeerIP", "192.168.1.105", buf, 512, path.c_str());
    g_config.peer_ip = buf;

    GetPrivateProfileStringA("General", "PeerPort", "8888", buf, 512, path.c_str());
    g_config.peer_port = atoi(buf);

    GetPrivateProfileStringA("General", "PVZPath", g_config.pvz_path.c_str(), buf, 512, path.c_str());
    g_config.pvz_path = buf;

    GetPrivateProfileStringA("General", "SaveFilePath", g_config.save_path.c_str(), buf, 512, path.c_str());
    g_config.save_path = buf;

    GetPrivateProfileStringA("General", "LocalBackupPath", g_config.local_backup_path.c_str(), buf, 512, path.c_str());
    g_config.local_backup_path = buf;

    GetPrivateProfileStringA("General", "RemoteBackupPath", g_config.remote_backup_path.c_str(), buf, 512, path.c_str());
    g_config.remote_backup_path = buf;
}

void SaveConfig(const std::string& path) {
    WritePrivateProfileStringA("General", "AllowAutoOpen", g_config.allow_auto_open ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("General", "Role", g_config.role.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "PeerIP", g_config.peer_ip.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "PeerPort", std::to_string(g_config.peer_port).c_str(), path.c_str());
    WritePrivateProfileStringA("General", "PVZPath", g_config.pvz_path.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "SaveFilePath", g_config.save_path.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "LocalBackupPath", g_config.local_backup_path.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "RemoteBackupPath", g_config.remote_backup_path.c_str(), path.c_str());
}

// ===================== 备份功能 =====================
bool CopyDirectory(const std::string& src_dir, const std::string& dest_dir) {
    if (!PathIsDirectoryA(dest_dir.c_str())) {
        _mkdir(dest_dir.c_str());
    }

    SHFILEOPSTRUCTA file_op;
    memset(&file_op, 0, sizeof(SHFILEOPSTRUCTA));
    file_op.wFunc = FO_COPY;
    file_op.pFrom = (src_dir + "\\*.*").c_str();
    file_op.pTo = dest_dir.c_str();
    file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_SILENT;

    return SHFileOperationA(&file_op) == 0;
}

time_t GetLatestFileTimeInDir(const std::string& dir_path) {
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA((dir_path + "\\*.*").c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    time_t latest_time = 0;
    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        ULARGE_INTEGER uli;
        uli.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
        time_t file_time = uli.QuadPart / 10000000 - 11644473600LL;

        if (file_time > latest_time) latest_time = file_time;
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
    return latest_time;
}

bool BackupSaveDir(const std::string& src_dir, const std::string& base_backup_dir) {
    if (!PathIsDirectoryA(src_dir.c_str())) {
        std::cerr << "存档文件夹不存在: " << src_dir << std::endl;
        return false;
    }

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, 32, "%Y%m%d_%H%M%S", localtime(&now));
    std::string dest_dir = base_backup_dir + "\\" + timestamp;

    return CopyDirectory(src_dir, dest_dir);
}

// ===================== PVZ功能 =====================
bool CheckPVZRunning() {
    HWND hwnd = FindWindowA(NULL, "Plants vs. Zombies");
    return hwnd != NULL;
}

bool StartPVZ() {
    if (CheckPVZRunning()) return true;
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    bool ret = CreateProcessA(
        g_config.pvz_path.c_str(), NULL, NULL, NULL, FALSE,
        0, NULL, NULL, &si, &pi
    );
    if (ret) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        g_state.pvz_running = true;
        if (g_state.connected) {
            std::string cmd = "START_PVZ";
            send(g_state.sock, cmd.c_str(), cmd.size(), 0);
        }
        return true;
    }
    return false;
}

void CapturePVZScreen() {
    HWND hwnd = FindWindowA(NULL, "Plants vs. Zombies");
    if (!hwnd) return;

    RECT rect;
    GetWindowRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcScreen, width, height);
    HGDIOBJ oldBmp = SelectObject(hdcMem, hbmMem);

    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, rect.left, rect.top, SRCCOPY);
    SelectObject(hdcMem, oldBmp);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::lock_guard<std::mutex> lock(g_state.mtx);
    if (g_state.screen_data) delete[] g_state.screen_data;
    g_state.screen_data = new unsigned char[width * height * 4];
    GetDIBits(hdcMem, hbmMem, 0, height, g_state.screen_data, &bmi, DIB_RGB_COLORS);

    g_state.screen_width = width;
    g_state.screen_height = height;

    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

// ===================== 网络功能 =====================
bool InitServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) { WSACleanup(); return false; }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_config.peer_port);

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(server_sock); WSACleanup(); return false;
    }

    if (listen(server_sock, 1) == SOCKET_ERROR) {
        closesocket(server_sock); WSACleanup(); return false;
    }

    std::cout << "服务端监听端口: " << g_config.peer_port << std::endl;
    sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    g_state.sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);
    closesocket(server_sock);

    if (g_state.sock == INVALID_SOCKET) { WSACleanup(); return false; }

    g_state.connected = true;
    std::cout << "已连接客户端: " << inet_ntoa(client_addr.sin_addr) << std::endl;
    return true;
}

bool InitClient() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    g_state.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_state.sock == INVALID_SOCKET) { WSACleanup(); return false; }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(g_config.peer_ip.c_str());
    server_addr.sin_port = htons(g_config.peer_port);

    if (connect(g_state.sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(g_state.sock); WSACleanup(); return false;
    }

    g_state.connected = true;
    std::cout << "已连接服务端: " << g_config.peer_ip << std::endl;
    return true;
}

void NetworkRecvThread() {
    char buf[4096];
    while (g_state.connected) {
        int recv_len = recv(g_state.sock, buf, 4096, 0);
        if (recv_len <= 0) {
            g_state.connected = false;
            closesocket(g_state.sock);
            g_state.sock = INVALID_SOCKET;
            std::cerr << "与对方断开连接!" << std::endl;
            break;
        }

        buf[recv_len] = '\0';
        std::string cmd = buf;

        if (cmd == "START_PVZ" && g_config.allow_auto_open) {
            StartPVZ();
        }
        else if (cmd == "BACKUP_LOCAL") {
            BackupSaveDir(g_config.save_path, g_config.local_backup_path);
        }
        else if (cmd == "BACKUP_REMOTE") {
            BackupSaveDir(g_config.save_path, g_config.remote_backup_path);
        }
        else if (cmd.find("SCREEN_DATA|") == 0) {
            // 后续扩展：解析屏幕数据
        }
    }
}

void ScreenSendThread() {
    while (g_state.connected && g_state.pvz_running) {
        CapturePVZScreen();
        std::string cmd = "SCREEN_DATA|" + std::to_string(g_state.screen_width) + "|" + std::to_string(g_state.screen_height);
        send(g_state.sock, cmd.c_str(), cmd.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ===================== Win32窗口和OpenGL初始化 =====================
bool CreateWin32Window(const char* title, int width, int height) {
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, WndProc, 0L, 0L, 
                       GetModuleHandle(NULL), NULL, NULL, NULL, NULL, 
                       "PVZSyncWindowClass", NULL };
    RegisterClassExA(&wc);
    
    g_hwnd = CreateWindowA("PVZSyncWindowClass", title, 
                           WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 
                           CW_USEDEFAULT, CW_USEDEFAULT, width, height, 
                           NULL, NULL, wc.hInstance, NULL);
    
    if (!g_hwnd) return false;

    // 获取DC
    g_hDC = GetDC(g_hwnd);

    // 设置像素格式
    PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 8, 0,
        PFD_MAIN_PLANE, 0, 0, 0, 0 };
    
    int pixelFormat = ChoosePixelFormat(g_hDC, &pfd);
    SetPixelFormat(g_hDC, pixelFormat, &pfd);

    // 创建OpenGL上下文
    g_hRC = wglCreateContext(g_hDC);
    wglMakeCurrent(g_hDC, g_hRC);

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    
    return true;
}

void CleanupWin32() {
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(g_hRC);
    ReleaseDC(g_hwnd, g_hDC);
    DestroyWindow(g_hwnd);
    UnregisterClassA("PVZSyncWindowClass", GetModuleHandle(NULL));
}

// ===================== GUI渲染 =====================
void RenderGUI() {
    // 开始ImGui帧
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(850, 650));
    ImGui::Begin("PVZ 内网联动工具");

    // 配置面板
    ImGui::CollapsingHeader("基础配置 [General]", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::Checkbox("随动打开PVZ", &g_config.allow_auto_open);

    char role_buf[32] = {0};
    strncpy(role_buf, g_config.role.c_str(), 31);
    ImGui::InputText("本机角色 (server/client)", role_buf, sizeof(role_buf));
    g_config.role = role_buf;

    char peer_ip_buf[32] = {0};
    strncpy(peer_ip_buf, g_config.peer_ip.c_str(), 31);
    ImGui::InputText("对方IP", peer_ip_buf, sizeof(peer_ip_buf));
    g_config.peer_ip = peer_ip_buf;

    ImGui::InputInt("通信端口", &g_config.peer_port);

    ImGui::CollapsingHeader("路径配置", ImGuiTreeNodeFlags_DefaultOpen);
    char pvz_path_buf[512] = {0};
    strncpy(pvz_path_buf, g_config.pvz_path.c_str(), 511);
    ImGui::InputText("PVZ程序路径", pvz_path_buf, sizeof(pvz_path_buf));
    g_config.pvz_path = pvz_path_buf;

    char save_path_buf[512] = {0};
    strncpy(save_path_buf, g_config.save_path.c_str(), 511);
    ImGui::InputText("存档文件夹路径", save_path_buf, sizeof(save_path_buf));
    g_config.save_path = save_path_buf;

    char local_backup_buf[512] = {0};
    strncpy(local_backup_buf, g_config.local_backup_path.c_str(), 511);
    ImGui::InputText("本地备份路径", local_backup_buf, sizeof(local_backup_buf));
    g_config.local_backup_path = local_backup_buf;

    char remote_backup_buf[512] = {0};
    strncpy(remote_backup_buf, g_config.remote_backup_path.c_str(), 511);
    ImGui::InputText("远程备份路径", remote_backup_buf, sizeof(remote_backup_buf));
    g_config.remote_backup_path = remote_backup_buf;

    // 状态显示
    ImGui::CollapsingHeader("运行状态", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::Text("连接状态: %s", g_state.connected ? "✅ 已连接" : "❌ 未连接");
    ImGui::Text("PVZ状态: %s", g_state.pvz_running ? "▶️ 运行中" : "⏹️ 未运行");
    if (PathIsDirectoryA(g_config.save_path.c_str())) {
        time_t latest_time = GetLatestFileTimeInDir(g_config.save_path);
        ImGui::Text("存档最新修改时间: %s", latest_time > 0 ? ctime(&latest_time) : "未知");
    }

    // 操作按钮
    ImGui::Spacing();
    if (ImGui::Button("保存配置到INI", ImVec2(120, 30))) {
        SaveConfig("pvz_config.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button("从INI加载配置", ImVec2(120, 30))) {
        ReadConfig("pvz_config.ini");
    }

    ImGui::SameLine();
    if (ImGui::Button("连接对方", ImVec2(100, 30)) && !g_state.connected) {
        std::thread([&]() {
            if (g_config.role == "server") InitServer();
            else InitClient();
            std::thread(&NetworkRecvThread).detach();
        }).detach();
    }

    ImGui::SameLine();
    if (ImGui::Button("启动PVZ", ImVec2(100, 30))) {
        StartPVZ();
        std::thread(&ScreenSendThread).detach();
    }

    ImGui::Spacing();
    if (ImGui::Button("备份存档到本地", ImVec2(150, 30))) {
        BackupSaveDir(g_config.save_path, g_config.local_backup_path);
    }
    ImGui::SameLine();
    if (ImGui::Button("发送远程备份指令", ImVec2(150, 30)) && g_state.connected) {
        send(g_state.sock, "BACKUP_REMOTE", 12, 0);
    }

    // 屏幕共享显示区
    ImGui::Spacing();
    ImGui::CollapsingHeader("屏幕共享", ImGuiTreeNodeFlags_DefaultOpen);
    if (g_state.screen_data && g_state.screen_width > 0 && g_state.screen_height > 0) {
        std::lock_guard<std::mutex> lock(g_state.mtx);
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_state.screen_width, g_state.screen_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, g_state.screen_data);
        ImGui::Image((void*)(intptr_t)tex, ImVec2(g_state.screen_width, g_state.screen_height));
        glDeleteTextures(1, &tex);
    } else {
        ImGui::Text("等待PVZ启动或屏幕数据传输...");
    }

    ImGui::End();
    ImGui::Render();

    // 清除并渲染
    glViewport(0, 0, 900, 700);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SwapBuffers(g_hDC);
}

// ===================== Win32消息处理 =====================
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_hDC != NULL && wParam != SIZE_MINIMIZED) {
            glViewport(0, 0, LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // 禁用ALT菜单
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        g_done = true;
        return 0;
    case WM_CLOSE:
        g_done = true;
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// ===================== 主函数 =====================
int main(int argc, char** argv) {
    // 创建Win32窗口和OpenGL上下文
    if (!CreateWin32Window("PVZ 内网联动工具", 900, 700)) {
        std::cerr << "创建窗口失败" << std::endl;
        return 1;
    }

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    ImGui::StyleColorsDark();
    
    // 初始化ImGui后端
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 加载配置
    ReadConfig("pvz_config.ini");

    // 主循环
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    
    while (!g_done) {
        // 处理消息
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                g_done = true;
        }
        
        if (g_done) break;

        // 更新PVZ状态
        g_state.pvz_running = CheckPVZRunning();

        // 渲染GUI
        RenderGUI();
    }

    // 清理资源
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    CleanupWin32();

    if (g_state.sock != INVALID_SOCKET) closesocket(g_state.sock);
    WSACleanup();
    if (g_state.screen_data) delete[] g_state.screen_data;

    return 0;
}