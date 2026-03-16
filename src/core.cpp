#include <core.h>

#include <shlwapi.h>
#include <direct.h>
#include <iostream>

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
    } else {
        std::cerr << "启动PVZ失败，错误码: " << GetLastError() << std::endl;
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
    } else {
        std::cerr << "备份失败" << std::endl;
    }

    return ret;
}