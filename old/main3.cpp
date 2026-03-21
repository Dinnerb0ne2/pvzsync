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
#include <tlhelp32.h>

// ImGui related - using Win32 backend
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_opengl3.h"

// OpenGL headers - Windows built-in
#include <GL/gl.h>

// 无控制台黑框
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

// Define GL_BGRA (Windows extension)
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

// Link necessary libraries
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "shlwapi.lib")

// Global configuration (strictly corresponds to [General] section in INI)
struct Config {
    bool allow_auto_open = true;          // Auto open toggle
    std::string role = "server";          // Role: server/client
    std::string peer_ip = "192.168.1.105";// Peer IP address
    int peer_port = 8888;                 // Communication port
    std::string pvz_path = "C:\\Program Files\\PlantsVsZombies\\PlantsVsZombies.exe";
    std::string save_path = "C:\\Users\\YourUsername\\AppData\\Roaming\\PopCap Games\\PlantsVsZombies\\userdata";
    std::string local_backup_path = "C:\\PVZBackup\\Local";
    std::string remote_backup_path = "C:\\PVZBackup\\Remote";
} g_config;

// Global state
struct State {
    SOCKET sock = INVALID_SOCKET;
    bool connected = false;
    bool pvz_running = false;
    bool last_pvz_running = false; // 用于检测PVZ关闭事件
    unsigned char* screen_data = nullptr;
    int screen_width = 0;
    int screen_height = 0;
    std::mutex mtx;
    bool exit_flag = false; // 退出标记
} g_state;

// Win32 global variables
HWND g_hwnd = NULL;
HDC g_hDC = NULL;
HGLRC g_hRC = NULL;
bool g_done = false;

// Forward declarations
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ===================== 工具函数 =====================
// 终止PVZ进程（快速关闭）
bool KillPVZ() {
    HWND pvz_hwnd = FindWindowA(NULL, "Plants vs. Zombies");
    if (!pvz_hwnd) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(pvz_hwnd, &pid);
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        TerminateProcess(hProcess, 0); // 强制终止，速度快
        CloseHandle(hProcess);
        return true;
    }
    return false;
}

// 终止本工具进程
void KillSelf() {
    HANDLE hSelf = OpenProcess(PROCESS_TERMINATE, FALSE, GetCurrentProcessId());
    if (hSelf) {
        TerminateProcess(hSelf, 0);
        CloseHandle(hSelf);
    }
}

// ===================== Config Read/Write =====================
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

// ===================== Backup Functions =====================
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
        return false;
    }

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, 32, "%Y%m%d_%H%M%S", localtime(&now));
    std::string dest_dir = base_backup_dir + "\\" + timestamp;

    return CopyDirectory(src_dir, dest_dir);
}

// 手动备份（对外接口）
void ManualBackup() {
    BackupSaveDir(g_config.save_path, g_config.local_backup_path);
}

// ===================== PVZ Functions =====================
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

// ===================== Network Functions =====================
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

    sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    g_state.sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);
    closesocket(server_sock);

    if (g_state.sock == INVALID_SOCKET) { WSACleanup(); return false; }

    g_state.connected = true;
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
    return true;
}

void NetworkRecvThread() {
    char buf[4096];
    while (g_state.connected && !g_state.exit_flag) {
        int recv_len = recv(g_state.sock, buf, 4096, 0);
        if (recv_len <= 0) {
            g_state.connected = false;
            closesocket(g_state.sock);
            g_state.sock = INVALID_SOCKET;
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
            // 屏幕数据解析（预留）
        }
        else if (cmd == "KILL_LOCAL") {
            // 关闭本机PVZ+工具
            KillPVZ();
            g_done = true;
            g_state.exit_flag = true;
        }
        else if (cmd == "KILL_ALL") {
            // 关闭本机PVZ+工具，同时通知对方
            KillPVZ();
            send(g_state.sock, "KILL_LOCAL", 10, 0); // 通知对方关闭自己
            g_done = true;
            g_state.exit_flag = true;
        }
        else if (cmd == "KILL_PEER_PVZ") {
            // 关闭对方PVZ（本机执行）
            KillPVZ();
        }
    }
}

void ScreenSendThread() {
    while (g_state.connected && g_state.pvz_running && !g_state.exit_flag) {
        CapturePVZScreen();
        std::string cmd = "SCREEN_DATA|" + std::to_string(g_state.screen_width) + "|" + std::to_string(g_state.screen_height);
        send(g_state.sock, cmd.c_str(), cmd.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// 发送关闭指令
void SendKillCmd(const std::string& cmd) {
    if (g_state.connected) {
        send(g_state.sock, cmd.c_str(), cmd.size(), 0);
    }
}

// ===================== Win32 Window and OpenGL Initialization =====================
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

    // Get DC
    g_hDC = GetDC(g_hwnd);

    // Set pixel format
    PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 8, 0,
        PFD_MAIN_PLANE, 0, 0, 0, 0 };
    
    int pixelFormat = ChoosePixelFormat(g_hDC, &pfd);
    SetPixelFormat(g_hDC, pixelFormat, &pfd);

    // Create OpenGL context
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

// ===================== GUI美化 + 渲染 =====================
void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    // 圆角
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;

    // 颜色主题（深蓝+浅灰）
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.25f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.35f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.45f, 0.85f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.08f, 0.45f, 0.85f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.55f, 0.95f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.05f, 0.35f, 0.75f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.15f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.45f, 0.85f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.2f, 0.2f, 0.3f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.22f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.2f, 0.2f, 0.3f, 1.0f);
}

void RenderGUI() {
    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // 主窗口
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(880, 700));
    ImGui::Begin("PVZ LAN Sync Tool", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // 1. 配置面板（折叠）
    ImGui::CollapsingHeader("Config", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PushItemWidth(300);
    ImGui::Checkbox("Auto Open PVZ", &g_config.allow_auto_open);
    ImGui::InputText("Role (server/client)", (char*)g_config.role.c_str(), 32);
    ImGui::InputText("Peer IP", (char*)g_config.peer_ip.c_str(), 32);
    ImGui::InputInt("Port", &g_config.peer_port);
    ImGui::InputText("PVZ Path", (char*)g_config.pvz_path.c_str(), 512);
    ImGui::InputText("Save Path", (char*)g_config.save_path.c_str(), 512);
    ImGui::InputText("Local Backup", (char*)g_config.local_backup_path.c_str(), 512);
    ImGui::InputText("Remote Backup", (char*)g_config.remote_backup_path.c_str(), 512);
    ImGui::PopItemWidth();

    // 2. 状态面板
    ImGui::Spacing();
    ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::Text("Connection: %s", g_state.connected ? "✅ Connected" : "❌ Disconnected");
    ImGui::Text("PVZ Status: %s", g_state.pvz_running ? "▶️ Running" : "⏹️ Stopped");
    if (PathIsDirectoryA(g_config.save_path.c_str())) {
        time_t latest_time = GetLatestFileTimeInDir(g_config.save_path);
        ImGui::Text("Save Last Mod: %s", latest_time > 0 ? ctime(&latest_time) : "Unknown");
    }

    // 3. 核心操作按钮（美化布局）
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Core Operations");
    ImGui::Spacing();

    // 按钮行1：连接 + 启动PVZ + 手动备份
    if (ImGui::Button("Connect", ImVec2(120, 40))) {
        if (!g_state.connected) {
            std::thread([&]() {
                if (g_config.role == "server") InitServer();
                else InitClient();
                std::thread(&NetworkRecvThread).detach();
            }).detach();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Start PVZ", ImVec2(120, 40))) {
        StartPVZ();
        std::thread(&ScreenSendThread).detach();
    }
    ImGui::SameLine();
    if (ImGui::Button("Backup Now", ImVec2(120, 40))) {
        ManualBackup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Config", ImVec2(120, 40))) {
        SaveConfig("pvz_config.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Config", ImVec2(120, 40))) {
        ReadConfig("pvz_config.ini");
    }

    // 按钮行2：关闭相关（核心需求）
    ImGui::Spacing();
    // 关闭本机PVZ+工具
    if (ImGui::Button("Close Local", ImVec2(140, 45))) {
        KillPVZ();
        g_done = true;
        g_state.exit_flag = true;
    }
    ImGui::SameLine();
    // 关闭所有（本机+对方）
    if (ImGui::Button("Close All", ImVec2(140, 45))) {
        SendKillCmd("KILL_ALL");
        KillPVZ();
        g_done = true;
        g_state.exit_flag = true;
    }
    ImGui::SameLine();
    // 关闭对方PVZ
    if (ImGui::Button("Close Peer", ImVec2(140, 45)) && g_state.connected) {
        SendKillCmd("KILL_PEER_PVZ");
    }
    ImGui::SameLine();
    // 保存配置 + 备份
    if (ImGui::Button("Save & Backup", ImVec2(140, 45))) {
        SaveConfig("pvz_config.ini");
        ManualBackup();
    }

    // 4. 屏幕共享面板
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::CollapsingHeader("Screen Share", ImGuiTreeNodeFlags_DefaultOpen);
    if (g_state.screen_data && g_state.screen_width > 0 && g_state.screen_height > 0) {
        std::lock_guard<std::mutex> lock(g_state.mtx);
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_state.screen_width, g_state.screen_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, g_state.screen_data);
        // 缩放显示，避免超出窗口
        ImVec2 display_size = ImVec2(
            std::min((float)g_state.screen_width, 800.0f),
            std::min((float)g_state.screen_height, 450.0f)
        );
        ImGui::Image((void*)(intptr_t)tex, display_size);
        glDeleteTextures(1, &tex);
    } else {
        ImGui::Text("Waiting for PVZ or screen data...");
    }

    ImGui::End();
    ImGui::Render();

    // 渲染到窗口
    glViewport(0, 0, 900, 700);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SwapBuffers(g_hDC);
}

// ===================== Win32 Message Handling =====================
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
        if ((wParam & 0xfff0) == SC_KEYMENU)
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

// ===================== Main Function =====================
int main(int argc, char** argv) {
    // 创建窗口
    if (!CreateWin32Window("PVZ LAN Sync Tool", 900, 750)) {
        return 1;
    }

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // 美化样式
    SetupImGuiStyle();
    
    // 初始化ImGui后端
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 加载配置
    ReadConfig("pvz_config.ini");

    // 主循环
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    
    while (!g_done && !g_state.exit_flag) {
        // 消息处理
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                g_done = true;
        }
        
        if (g_done || g_state.exit_flag) break;

        // 检测PVZ状态变化（关闭时自动备份）
        g_state.pvz_running = CheckPVZRunning();
        if (g_state.last_pvz_running && !g_state.pvz_running) {
            // PVZ从运行变关闭，自动备份
            BackupSaveDir(g_config.save_path, g_config.local_backup_path);
        }
        g_state.last_pvz_running = g_state.pvz_running;

        // 渲染GUI
        RenderGUI();
    }

    // 资源清理
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    CleanupWin32();

    if (g_state.sock != INVALID_SOCKET) closesocket(g_state.sock);
    WSACleanup();
    if (g_state.screen_data) delete[] g_state.screen_data;

    // 强制退出（确保快速关闭）
    KillSelf();
    return 0;
}