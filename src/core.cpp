#include <core.h>
#include <ui.h>

#include <shlwapi.h>
#include <direct.h>
#include <iostream>
#include <tlhelp32.h>
#include <algorithm>

// 全局变量定义
Config g_config;
PVZState g_pvz_state;

// 读取INI配置
void ReadConfig(const std::string& path) {
    char buf[512] = {0};
    
    // 基础配置
    GetPrivateProfileStringA("General", "AllowAutoOpen", "1", buf, 512, path.c_str());
    g_config.allow_auto_open = (atoi(buf) == 1);

    GetPrivateProfileStringA("General", "Role", "server", buf, 512, path.c_str());
    g_config.role = buf;

    GetPrivateProfileStringA("General", "PeerIP", "192.168.1.105", buf, 512, path.c_str());
    g_config.peer_ip = buf;

    GetPrivateProfileStringA("General", "PeerPort", "8888", buf, 512, path.c_str());
    g_config.peer_port = atoi(buf);

    // 路径配置
    GetPrivateProfileStringA("General", "PVZPath", g_config.pvz_path.c_str(), buf, 512, path.c_str());
    g_config.pvz_path = buf;

    GetPrivateProfileStringA("General", "SaveFilePath", g_config.save_path.c_str(), buf, 512, path.c_str());
    g_config.save_path = buf;

    GetPrivateProfileStringA("General", "LocalBackupPath", g_config.local_backup_path.c_str(), buf, 512, path.c_str());
    g_config.local_backup_path = buf;

    GetPrivateProfileStringA("General", "RemoteBackupPath", g_config.remote_backup_path.c_str(), buf, 512, path.c_str());
    g_config.remote_backup_path = buf;

    // 远程控制配置
    GetPrivateProfileStringA("General", "Resolution", "720p", buf, 512, path.c_str());
    g_config.resolution = buf;

    GetPrivateProfileStringA("General", "Framerate", "25fps", buf, 512, path.c_str());
    g_config.framerate = buf;

    GetPrivateProfileStringA("General", "TargetProcess", "pvzHE.exe", buf, 512, path.c_str());
    g_config.target_process = buf;
}

// 保存INI配置
void SaveConfig(const std::string& path) {
    WritePrivateProfileStringA("General", "AllowAutoOpen", g_config.allow_auto_open ? "1" : "0", path.c_str());
    WritePrivateProfileStringA("General", "Role", g_config.role.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "PeerIP", g_config.peer_ip.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "PeerPort", std::to_string(g_config.peer_port).c_str(), path.c_str());
    WritePrivateProfileStringA("General", "PVZPath", g_config.pvz_path.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "SaveFilePath", g_config.save_path.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "LocalBackupPath", g_config.local_backup_path.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "RemoteBackupPath", g_config.remote_backup_path.c_str(), path.c_str());
    
    // 远程控制配置
    WritePrivateProfileStringA("General", "Resolution", g_config.resolution.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "Framerate", g_config.framerate.c_str(), path.c_str());
    WritePrivateProfileStringA("General", "TargetProcess", g_config.target_process.c_str(), path.c_str());
}

// 检查PVZ是否运行
bool CheckPVZRunning() {
    HWND pvz_hwnd = FindWindowA(NULL, "Plants vs. Zombies");
    g_pvz_state.is_running = (pvz_hwnd != NULL);
    return g_pvz_state.is_running;
}

// 启动PVZ进程
bool StartPVZ(const std::string& pvz_path) {
    if (CheckPVZRunning()) return true;

    STARTUPINFOA si = {0};
    si.cb = sizeof(STARTUPINFOA);
    PROCESS_INFORMATION pi = {0};

    bool ret = CreateProcessA(
        pvz_path.c_str(),
        NULL,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (ret) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        g_pvz_state.is_running = true;
        AddMessage("PVZ启动成功", MessageType::Success);
    } else {
        std::cerr << "启动PVZ失败，错误码: " << GetLastError() << std::endl;
        AddMessage("PVZ启动失败，错误码: " + std::to_string(GetLastError()), MessageType::Error);
    }

    return ret;
}

// 复制文件夹（Windows API）
bool CopyDirectory(const std::string& src_dir, const std::string& dest_dir) {
    if (!PathIsDirectoryA(src_dir.c_str())) {
        std::cerr << "源目录不存在: " << src_dir << std::endl;
        return false;
    }

    if (!PathIsDirectoryA(dest_dir.c_str())) {
        _mkdir(dest_dir.c_str());
    }

    SHFILEOPSTRUCTA file_op = {0};
    file_op.wFunc = FO_COPY;
    file_op.pFrom = (src_dir + "\\*.*").c_str();
    file_op.pTo = dest_dir.c_str();
    file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_SILENT;

    int ret = SHFileOperationA(&file_op);
    return (ret == 0);
}

// 获取文件夹最新文件时间
time_t GetLatestFileTimeInDir(const std::string& dir_path) {
    WIN32_FIND_DATAA find_data = {0};
    HANDLE hFind = FindFirstFileA((dir_path + "\\*.*").c_str(), &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }

    time_t latest_time = 0;
    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        ULARGE_INTEGER uli;
        uli.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
        time_t file_time = uli.QuadPart / 10000000 - 11644473600LL;

        if (file_time > latest_time) {
            latest_time = file_time;
        }
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
    return latest_time;
}

// 备份存档（带时间戳）
bool BackupSaveDir(const std::string& src_dir, const std::string& base_backup_dir) {
    if (!PathIsDirectoryA(src_dir.c_str())) {
        std::cerr << "存档目录不存在: " << src_dir << std::endl;
        return false;
    }

    if (!PathIsDirectoryA(base_backup_dir.c_str())) {
        _mkdir(base_backup_dir.c_str());
    }

    time_t now = time(NULL);
    char timestamp[32] = {0};
    strftime(timestamp, 32, "%Y%m%d_%H%M%S", localtime(&now));
    std::string dest_dir = base_backup_dir + "\\" + timestamp;

    bool ret = CopyDirectory(src_dir, dest_dir);
    if (ret) {
        std::cout << "备份成功: " << dest_dir << std::endl;
        AddMessage("备份存档成功", MessageType::Success);
    } else {
        std::cerr << "备份失败" << std::endl;
        AddMessage("备份存档失败", MessageType::Error);
    }

    return ret;
}

// 关闭指定进程
bool CloseProcessByName(const std::string& process_name) {
    // 创建进程快照
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // 遍历进程
    if (Process32First(hSnapshot, &pe32)) {
        do {
            // 比较进程名（不区分大小写）
            std::string current_name = pe32.szExeFile;
            std::transform(current_name.begin(), current_name.end(), current_name.begin(), ::tolower);
            std::string target_name = process_name;
            std::transform(target_name.begin(), target_name.end(), target_name.begin(), ::tolower);

            if (current_name == target_name) {
                // 打开进程
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hProcess) {
                    // 终止进程
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                    CloseHandle(hSnapshot);
                    return true;
                }
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return false;
}

// 关闭自己和目标进程
bool CloseSelfAndTarget() {
    // 关闭目标进程
    bool closed = false;
    if (!g_config.target_process.empty()) {
        closed = CloseProcessByName(g_config.target_process);
        if (closed) {
            AddMessage("已关闭目标进程: " + g_config.target_process, MessageType::Success);
        }
    }

    // 关闭自己
    CloseApp();
    return closed;
}

// 关闭应用程序
void CloseApp() {
    // 发送关闭消息
    PostQuitMessage(0);
}