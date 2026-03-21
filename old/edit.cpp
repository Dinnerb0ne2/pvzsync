#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <windows.h>
#include <ctime>
#include <direct.h>
#include <shlwapi.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx9.h"
#include <d3d9.h>

// 链接库
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "kernel32.lib")

// ===================== 全局配置（仅从INI读2项）=====================
struct GlobalConfig {
    std::string SaveFilePath;  // 存档路径
    std::string BackupDir;     // 备份目录
} g_global_cfg;

// ===================== 修改参数（GUI实时修改）=====================
struct ModifyParams {
    // 基础关卡
    int main_level = -1;       // 主关卡(1-99)，-1不修改
    int diff = -1;             // 难度(0-2)，-1不修改
    // 资源
    int gold_base = -1;        // 金币基数(0-255)，-1不修改
    int diamond = -1;          // 钻石(0-9999)，-1不修改
    int sun_cap = -1;          // 阳光上限(200-999)，-1不修改
    int gold_rate = -1;        // 金币倍率(1-10)，-1不修改
    // 场景解锁
    bool basic_garden = false;
    bool mushroom_garden = false;
    bool water_garden = false;
    bool zen_garden = false;
    bool world_tree = false;
    // 世界树
    int wt_level = -1;         // 等级(1-200)，-1不修改
    int wt_floor = -1;         // 层数(1-999)，-1不修改
    int wt_gold = -1;          // 世界树金币(0-999999)，-1不修改
    // 副本进度
    int endless = -1;          // 无尽波数(1-999)，-1不修改
    int challenge = -1;        // 挑战关卡(1-50)，-1不修改
    int puzzle = -1;           // 解谜关卡(1-30)，-1不修改
    int survive = -1;          // 生存关卡(1-20)，-1不修改
    // 操作日志
    std::string log = "欢迎使用PVZ杂交版修改工具！\n1. 确认存档路径正确\n2. 配置修改参数\n3. 点击【执行修改】";
} g_modify_params;

// ===================== 全局变量（ImGui/D3D）=====================
LPDIRECT3D9              g_pD3D = nullptr;
LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
HWND                     g_hWnd = nullptr;
bool                     g_bRunning = true;
std::vector<unsigned char> g_save_data; // 存档二进制数据

// ===================== 函数声明 =====================
// INI配置读取
void LoadGlobalConfig();
// 存档操作
bool ReadSave();
bool WriteSave();
bool AutoBackupSave();
bool CheckSaveValid();
// 核心修改
void ModifySaveData();
// ImGui界面渲染
void RenderGUI();
// D3D/窗口初始化
bool InitD3DWindow();
void CleanupD3DWindow();
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// 日志添加
void AddLog(const std::string& msg);

// ===================== INI配置读取（仅读2项）=====================
void LoadGlobalConfig() {
    const char* cfg_path = "pvz_he_config.ini";
    // 读存档路径
    char save_path[1024] = {0};
    GetPrivateProfileStringA("Global", "SaveFilePath", 
        "C:\\ProgramData\\PopCap Games\\PlantsVsZombies\\pvzHE\\yourdata\\game1_0.dat",
        save_path, 1024, cfg_path);
    g_global_cfg.SaveFilePath = save_path;
    // 读备份目录
    char backup_dir[1024] = {0};
    GetPrivateProfileStringA("Global", "BackupDir", 
        "./PVZ_HE_Backup", backup_dir, 1024, cfg_path);
    g_global_cfg.BackupDir = backup_dir;
    AddLog("配置加载完成：存档路径=" + g_global_cfg.SaveFilePath);
}

// ===================== 存档操作 =====================
bool AutoBackupSave() {
    if (!PathIsDirectoryA(g_global_cfg.BackupDir.c_str())) {
        if (_mkdir(g_global_cfg.BackupDir.c_str()) != 0) {
            AddLog("❌ 创建备份目录失败：" + g_global_cfg.BackupDir);
            return false;
        }
    }
    // 时间戳命名
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, 32, "%Y%m%d_%H%M%S", localtime(&now));
    std::string backup_path = g_global_cfg.BackupDir + "\\" + timestamp + "_game1_0.dat";
    // 复制存档
    SHFILEOPSTRUCTA file_op = {0};
    file_op.wFunc = FO_COPY;
    file_op.pFrom = g_global_cfg.SaveFilePath.c_str();
    file_op.pTo = backup_path.c_str();
    file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_SILENT;
    if (SHFileOperationA(&file_op) != 0) {
        AddLog("❌ 存档备份失败！");
        return false;
    }
    AddLog("✅ 存档备份完成：" + backup_path);
    return true;
}

bool CheckSaveValid() {
    if (g_save_data.size() < 161) {
        AddLog("❌ 存档非法：文件大小过小");
        return false;
    }
    if (g_save_data[0x04] == 0xFF && g_save_data[0x05] == 0xFF) {
        AddLog("❌ 存档非法：关键数据异常");
        return false;
    }
    return true;
}

bool ReadSave() {
    std::ifstream file(g_global_cfg.SaveFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        AddLog("❌ 打开存档失败：" + g_global_cfg.SaveFilePath);
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    g_save_data.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(g_save_data.data()), size)) {
        g_save_data.clear();
        AddLog("❌ 读取存档数据失败！");
        return false;
    }
    file.close();
    AddLog("✅ 读取存档成功：大小=" + std::to_string(size) + "字节");
    return true;
}

bool WriteSave() {
    std::ofstream file(g_global_cfg.SaveFilePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        AddLog("❌ 写入存档失败：权限不足/文件被占用");
        return false;
    }
    if (!file.write(reinterpret_cast<const char*>(g_save_data.data()), g_save_data.size())) {
        AddLog("❌ 写入存档数据失败！");
        return false;
    }
    file.close();
    AddLog("✅ 写入存档成功！");
    return true;
}

// ===================== 核心修改逻辑 =====================
void ModifySaveData() {
    if (!CheckSaveValid()) return;

    // 1. 基础关卡
    if (g_modify_params.main_level >= 1 && g_modify_params.main_level <= 99) {
        g_save_data[0x04] = static_cast<unsigned char>(g_modify_params.main_level);
        AddLog("✅ 主关卡修改为：" + std::to_string(g_modify_params.main_level));
    }
    if (g_modify_params.diff >= 0 && g_modify_params.diff <= 2) {
        g_save_data[0x05] = static_cast<unsigned char>(g_modify_params.diff);
        std::string diff_str = (g_modify_params.diff == 0 ? "普通" : (g_modify_params.diff == 1 ? "困难" : "炼狱"));
        AddLog("✅ 难度修改为：" + diff_str);
    }

    // 2. 资源
    if (g_modify_params.gold_base >= 0 && g_modify_params.gold_base <= 255) {
        g_save_data[0x08] = static_cast<unsigned char>(g_modify_params.gold_base);
        AddLog("✅ 金币修改为：" + std::to_string(g_modify_params.gold_base * 10) + "（基数=" + std::to_string(g_modify_params.gold_base) + "）");
    }
    if (g_modify_params.diamond >= 0 && g_modify_params.diamond <= 9999) {
        g_save_data[0x09] = static_cast<unsigned char>(g_modify_params.diamond & 0xFF);
        g_save_data[0x0A - 1] = static_cast<unsigned char>((g_modify_params.diamond >> 8) & 0xFF);
        AddLog("✅ 钻石修改为：" + std::to_string(g_modify_params.diamond));
    }
    if (g_modify_params.sun_cap >= 200 && g_modify_params.sun_cap <= 999) {
        g_save_data[0x0A] = static_cast<unsigned char>(g_modify_params.sun_cap);
        AddLog("✅ 阳光上限修改为：" + std::to_string(g_modify_params.sun_cap));
    }
    if (g_modify_params.gold_rate >= 1 && g_modify_params.gold_rate <= 10) {
        g_save_data[0x0B] = static_cast<unsigned char>(g_modify_params.gold_rate);
        AddLog("✅ 金币倍率修改为：" + std::to_string(g_modify_params.gold_rate) + "倍");
    }

    // 3. 场景解锁
    int unlock_cnt = 0;
    if (g_modify_params.basic_garden) { g_save_data[0x10] = 1; unlock_cnt++; }
    if (g_modify_params.mushroom_garden) { g_save_data[0x11] = 1; unlock_cnt++; }
    if (g_modify_params.water_garden) { g_save_data[0x12] = 1; unlock_cnt++; }
    if (g_modify_params.zen_garden) { g_save_data[0x13] = 1; unlock_cnt++; }
    if (g_modify_params.world_tree) { g_save_data[0x14] = 1; unlock_cnt++; }
    if (unlock_cnt > 0) AddLog("✅ 解锁场景数：" + std::to_string(unlock_cnt));

    // 4. 世界树
    if (g_modify_params.wt_level >= 1 && g_modify_params.wt_level <= 200) {
        g_save_data[0x18] = static_cast<unsigned char>(g_modify_params.wt_level & 0xFF);
        g_save_data[0x19] = static_cast<unsigned char>((g_modify_params.wt_level >> 8) & 0xFF);
        AddLog("✅ 世界树等级修改为：" + std::to_string(g_modify_params.wt_level));
    }
    if (g_modify_params.wt_floor >= 1 && g_modify_params.wt_floor <= 999) {
        g_save_data[0x1A] = static_cast<unsigned char>(g_modify_params.wt_floor & 0xFF);
        g_save_data[0x1B] = static_cast<unsigned char>((g_modify_params.wt_floor >> 8) & 0xFF);
        AddLog("✅ 世界树层数修改为：" + std::to_string(g_modify_params.wt_floor));
    }
    if (g_modify_params.wt_gold >= 0 && g_modify_params.wt_gold <= 999999) {
        g_save_data[0x1C] = static_cast<unsigned char>(g_modify_params.wt_gold & 0xFF);
        g_save_data[0x1D] = static_cast<unsigned char>((g_modify_params.wt_gold >> 8) & 0xFF);
        AddLog("✅ 世界树金币修改为：" + std::to_string(g_modify_params.wt_gold));
    }

    // 5. 副本进度
    if (g_modify_params.endless >= 1 && g_modify_params.endless <= 999) {
        g_save_data[0x20] = static_cast<unsigned char>(g_modify_params.endless & 0xFF);
        g_save_data[0x21] = static_cast<unsigned char>((g_modify_params.endless >> 8) & 0xFF);
        AddLog("✅ 无尽波数修改为：" + std::to_string(g_modify_params.endless));
    }
    if (g_modify_params.challenge >= 1 && g_modify_params.challenge <= 50) {
        g_save_data[0x22] = static_cast<unsigned char>(g_modify_params.challenge);
        AddLog("✅ 挑战关卡修改为：" + std::to_string(g_modify_params.challenge));
    }
    if (g_modify_params.puzzle >= 1 && g_modify_params.puzzle <= 30) {
        g_save_data[0x23] = static_cast<unsigned char>(g_modify_params.puzzle);
        AddLog("✅ 解谜关卡修改为：" + std::to_string(g_modify_params.puzzle));
    }
    if (g_modify_params.survive >= 1 && g_modify_params.survive <= 20) {
        g_save_data[0x24] = static_cast<unsigned char>(g_modify_params.survive);
        AddLog("✅ 生存关卡修改为：" + std::to_string(g_modify_params.survive));
    }

    AddLog("✅ 所有修改项执行完成！");
}

// ===================== 日志工具 =====================
void AddLog(const std::string& msg) {
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, 32, "[%H:%M:%S]", localtime(&now));
    g_modify_params.log += "\n" + std::string(timestamp) + " " + msg;
}

// ===================== ImGui界面渲染 =====================
void RenderGUI() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(800, 700), ImGuiCond_Once);
    ImGui::Begin("PVZ经典杂交版通用修改工具", &g_bRunning, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // 1. 存档路径显示
    ImGui::Text("📁 存档路径：");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", g_global_cfg.SaveFilePath.c_str());
    ImGui::Separator();

    // 2. 基础关卡进度
    ImGui::Text("🎮 基础关卡进度");
    ImGui::PushItemWidth(200);
    ImGui::InputInt("主关卡进度(1-99/-1不修改)", &g_modify_params.main_level);
    ImGui::InputInt("关卡难度(0普通/1困难/2炼狱/-1不修改)", &g_modify_params.diff);
    ImGui::PopItemWidth();
    ImGui::Separator();

    // 3. 资源修改
    ImGui::Text("💰 资源修改");
    ImGui::PushItemWidth(200);
    ImGui::InputInt("金币基数(0-255/-1不修改，实际=基数×10)", &g_modify_params.gold_base);
    ImGui::InputInt("钻石(0-9999/-1不修改)", &g_modify_params.diamond);
    ImGui::InputInt("阳光上限(200-999/-1不修改)", &g_modify_params.sun_cap);
    ImGui::InputInt("花园金币倍率(1-10/-1不修改)", &g_modify_params.gold_rate);
    ImGui::PopItemWidth();
    ImGui::Separator();

    // 4. 场景解锁
    ImGui::Text("🏡 场景解锁（勾选=解锁）");
    ImGui::Checkbox("基础花园", &g_modify_params.basic_garden);
    ImGui::SameLine();
    ImGui::Checkbox("蘑菇花园", &g_modify_params.mushroom_garden);
    ImGui::SameLine();
    ImGui::Checkbox("水生花园", &g_modify_params.water_garden);
    ImGui::SameLine();
    ImGui::Checkbox("禅意花园", &g_modify_params.zen_garden);
    ImGui::SameLine();
    ImGui::Checkbox("世界树", &g_modify_params.world_tree);
    ImGui::Separator();

    // 5. 世界树修改
    ImGui::Text("🌳 世界树修改");
    ImGui::PushItemWidth(200);
    ImGui::InputInt("世界树等级(1-200/-1不修改)", &g_modify_params.wt_level);
    ImGui::InputInt("世界树层数(1-999/-1不修改)", &g_modify_params.wt_floor);
    ImGui::InputInt("世界树金币(0-999999/-1不修改)", &g_modify_params.wt_gold);
    ImGui::PopItemWidth();
    ImGui::Separator();

    // 6. 副本进度
    ImGui::Text("⚔️ 副本进度");
    ImGui::PushItemWidth(200);
    ImGui::InputInt("无尽模式波数(1-999/-1不修改)", &g_modify_params.endless);
    ImGui::InputInt("挑战模式关卡(1-50/-1不修改)", &g_modify_params.challenge);
    ImGui::InputInt("解谜模式关卡(1-30/-1不修改)", &g_modify_params.puzzle);
    ImGui::InputInt("生存模式关卡(1-20/-1不修改)", &g_modify_params.survive);
    ImGui::PopItemWidth();
    ImGui::Separator();

    // 7. 操作按钮
    ImGui::Text("⚠️  操作前请关闭游戏！");
    if (ImGui::Button("📤 读取存档", ImVec2(120, 40))) {
        ReadSave();
    }
    ImGui::SameLine();
    if (ImGui::Button("🚀 执行修改", ImVec2(120, 40))) {
        if (g_save_data.empty()) {
            AddLog("❌ 请先读取存档！");
        } else {
            if (AutoBackupSave()) {
                ModifySaveData();
                WriteSave();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("🗑️ 清空日志", ImVec2(120, 40))) {
        g_modify_params.log = "";
    }
    ImGui::Separator();

    // 8. 日志显示
    ImGui::Text("📜 操作日志");
    ImGui::BeginChild("LogWindow", ImVec2(0, 200), true);
    ImGui::TextUnformatted(g_modify_params.log.c_str());
    ImGui::EndChild();

    ImGui::End();
}

// ===================== D3D/窗口初始化 =====================
bool InitD3DWindow() {
    // 创建窗口
    WNDCLASSEXA wc = {sizeof(WNDCLASSEXA), CS_CLASSDC, WndProc, 0L, 0L,
                      GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
                      "PVZModifyToolClass", NULL};
    RegisterClassExA(&wc);
    g_hWnd = CreateWindowExA(0, wc.lpszClassName, "PVZ经典杂交版通用修改工具",
                            WS_OVERLAPPEDWINDOW, 100, 100, 820, 740,
                            NULL, NULL, wc.hInstance, NULL);

    // 初始化D3D
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;

    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hWnd,
                            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                            &d3dpp, &g_pd3dDevice) < 0)
        return false;

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);

    return true;
}

void CleanupD3DWindow() {
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_pd3dDevice) g_pd3dDevice->Release();
    if (g_pD3D) g_pD3D->Release();
    DestroyWindow(g_hWnd);
    UnregisterClassA("PVZModifyToolClass", GetModuleHandle(NULL));
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            g_bRunning = false;
            return 0;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

// ===================== 主函数 =====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 加载全局配置（仅存档路径+备份目录）
    LoadGlobalConfig();

    // 初始化D3D+ImGui
    if (!InitD3DWindow()) {
        MessageBox(NULL, "初始化界面失败！", "错误", MB_ICONERROR);
        return 1;
    }

    // 主循环
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (g_bRunning && msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // 开始ImGui帧
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 渲染GUI
        RenderGUI();

        // 渲染
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(20, 20, 20, 255);
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
    }

    // 清理
    CleanupD3DWindow();
    return 0;
}