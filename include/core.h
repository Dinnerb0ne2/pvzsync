#pragma once
#include <string>
#include <windows.h>
#include <ctime>

// 配置结构体
struct Config {
    bool allow_auto_open = true;          
    std::string role = "server";          
    std::string peer_ip = "192.168.1.105";
    int peer_port = 8888;                 
    std::string pvz_path = "C:\\Program Files\\PlantsVsZombies\\PlantsVsZombies.exe";
    std::string save_path = "C:\\Users\\你的用户名\\AppData\\Roaming\\PopCap Games\\PlantsVsZombies\\userdata";
    std::string local_backup_path = "C:\\PVZBackup\\Local";
    std::string remote_backup_path = "C:\\PVZBackup\\Remote";
    
    // 远程控制配置
    std::string resolution = "720p";      // 分辨率：540p/720p/1080p
    std::string framerate = "25fps";      // 帧率：25fps/30fps/45fps/60fps
    std::string target_process = "pvzHE.exe";  // 目标进程名
};

// PVZ运行状态
struct PVZState {
    bool is_running = false;
};

// 全局变量声明（避免重复定义）
extern Config g_config;
extern PVZState g_pvz_state;

// 配置读写
void ReadConfig(const std::string& path);
void SaveConfig(const std::string& path);

// PVZ控制
bool CheckPVZRunning();
bool StartPVZ(const std::string& pvz_path);

// 备份相关
bool CopyDirectory(const std::string& src_dir, const std::string& dest_dir);
time_t GetLatestFileTimeInDir(const std::string& dir_path);
bool BackupSaveDir(const std::string& src_dir, const std::string& base_backup_dir);