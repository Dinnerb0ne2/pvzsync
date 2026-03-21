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

// ImGui related - using Win32 backend
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_opengl3.h"

// OpenGL headers - Windows built-in
#include <GL/gl.h>

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
    unsigned char* screen_data = nullptr;
    int screen_width = 0;
    int screen_height = 0;
    std::mutex mtx;
} g_state;

// Win32 global variables
HWND g_hwnd = NULL;
HDC g_hDC = NULL;
HGLRC g_hRC = NULL;
bool g_done = false;

// Forward declarations
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
        std::cerr << "Save directory does not exist: " << src_dir << std::endl;
        return false;
    }

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, 32, "%Y%m%d_%H%M%S", localtime(&now));
    std::string dest_dir = base_backup_dir + "\\" + timestamp;

    return CopyDirectory(src_dir, dest_dir);
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

    std::cout << "Server listening on port: " << g_config.peer_port << std::endl;
    sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    g_state.sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);
    closesocket(server_sock);

    if (g_state.sock == INVALID_SOCKET) { WSACleanup(); return false; }

    g_state.connected = true;
    std::cout << "Client connected: " << inet_ntoa(client_addr.sin_addr) << std::endl;
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
    std::cout << "Connected to server: " << g_config.peer_ip << std::endl;
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
            std::cerr << "Disconnected from peer!" << std::endl;
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
            // Future expansion: Parse screen data
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

// ===================== GUI Rendering =====================
void RenderGUI() {
    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(850, 650));
    ImGui::Begin("PVZ LAN Sync Tool");

    // Configuration panel
    ImGui::CollapsingHeader("Basic Configuration [General]", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::Checkbox("Auto open PVZ", &g_config.allow_auto_open);

    char role_buf[32] = {0};
    strncpy(role_buf, g_config.role.c_str(), 31);
    ImGui::InputText("Local Role (server/client)", role_buf, sizeof(role_buf));
    g_config.role = role_buf;

    char peer_ip_buf[32] = {0};
    strncpy(peer_ip_buf, g_config.peer_ip.c_str(), 31);
    ImGui::InputText("Peer IP Address", peer_ip_buf, sizeof(peer_ip_buf));
    g_config.peer_ip = peer_ip_buf;

    ImGui::InputInt("Communication Port", &g_config.peer_port);

    ImGui::CollapsingHeader("Path Configuration", ImGuiTreeNodeFlags_DefaultOpen);
    char pvz_path_buf[512] = {0};
    strncpy(pvz_path_buf, g_config.pvz_path.c_str(), 511);
    ImGui::InputText("PVZ Executable Path", pvz_path_buf, sizeof(pvz_path_buf));
    g_config.pvz_path = pvz_path_buf;

    char save_path_buf[512] = {0};
    strncpy(save_path_buf, g_config.save_path.c_str(), 511);
    ImGui::InputText("Save File Path", save_path_buf, sizeof(save_path_buf));
    g_config.save_path = save_path_buf;

    char local_backup_buf[512] = {0};
    strncpy(local_backup_buf, g_config.local_backup_path.c_str(), 511);
    ImGui::InputText("Local Backup Path", local_backup_buf, sizeof(local_backup_buf));
    g_config.local_backup_path = local_backup_buf;

    char remote_backup_buf[512] = {0};
    strncpy(remote_backup_buf, g_config.remote_backup_path.c_str(), 511);
    ImGui::InputText("Remote Backup Path", remote_backup_buf, sizeof(remote_backup_buf));
    g_config.remote_backup_path = remote_backup_buf;

    // Status display
    ImGui::CollapsingHeader("Running Status", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::Text("Connection Status: %s", g_state.connected ? "✅ Connected" : "❌ Disconnected");
    ImGui::Text("PVZ Status: %s", g_state.pvz_running ? "▶️ Running" : "⏹️ Not running");
    if (PathIsDirectoryA(g_config.save_path.c_str())) {
        time_t latest_time = GetLatestFileTimeInDir(g_config.save_path);
        ImGui::Text("Save File Last Modified: %s", latest_time > 0 ? ctime(&latest_time) : "Unknown");
    }

    // Operation buttons
    ImGui::Spacing();
    if (ImGui::Button("Save Config to INI", ImVec2(120, 30))) {
        SaveConfig("pvz_config.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Config from INI", ImVec2(120, 30))) {
        ReadConfig("pvz_config.ini");
    }

    ImGui::SameLine();
    if (ImGui::Button("Connect to Peer", ImVec2(100, 30)) && !g_state.connected) {
        std::thread([&]() {
            if (g_config.role == "server") InitServer();
            else InitClient();
            std::thread(&NetworkRecvThread).detach();
        }).detach();
    }

    ImGui::SameLine();
    if (ImGui::Button("Start PVZ", ImVec2(100, 30))) {
        StartPVZ();
        std::thread(&ScreenSendThread).detach();
    }

    ImGui::Spacing();
    if (ImGui::Button("Backup Save to Local", ImVec2(150, 30))) {
        BackupSaveDir(g_config.save_path, g_config.local_backup_path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Send Remote Backup Cmd", ImVec2(150, 30)) && g_state.connected) {
        send(g_state.sock, "BACKUP_REMOTE", 12, 0);
    }

    // Screen sharing display area
    ImGui::Spacing();
    ImGui::CollapsingHeader("Screen Sharing", ImGuiTreeNodeFlags_DefaultOpen);
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
        ImGui::Text("Waiting for PVZ startup or screen data transmission...");
    }

    ImGui::End();
    ImGui::Render();

    // Clear and render
    glViewport(0, 0, 900, 700);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
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
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT menu
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
    // Create Win32 window and OpenGL context
    if (!CreateWin32Window("PVZ LAN Sync Tool", 900, 700)) {
        std::cerr << "Failed to create window" << std::endl;
        return 1;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    ImGui::StyleColorsDark();
    
    // Initialize ImGui backends
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Load configuration
    ReadConfig("pvz_config.ini");

    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    
    while (!g_done) {
        // Process messages
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                g_done = true;
        }
        
        if (g_done) break;

        // Update PVZ status
        g_state.pvz_running = CheckPVZRunning();

        // Render GUI
        RenderGUI();
    }

    // Cleanup resources
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    CleanupWin32();

    if (g_state.sock != INVALID_SOCKET) closesocket(g_state.sock);
    WSACleanup();
    if (g_state.screen_data) delete[] g_state.screen_data;

    return 0;
}