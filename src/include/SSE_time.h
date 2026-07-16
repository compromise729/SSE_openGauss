#ifndef TIME_TRACKER_H
#define TIME_TRACKER_H

#include <chrono>
#include <string>
#include <unordered_map>
#include <fstream>
#include <mutex>

class TimeTracker {
public:
    // 获取单例实例
    static TimeTracker& getInstance();

    // 设置日志文件路径（可选，不调用时使用默认日志文件）
    void setLogFile(const std::string& fileName);

    // 开始计时
    void start(const std::string& taskName);

    // 停止计时并记录到日志
    void stop(const std::string& taskName);

private:
    TimeTracker(); // 构造函数
    ~TimeTracker(); // 析构函数

    struct TaskTime {
        std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
        bool isActive = false;
    };

    std::unordered_map<std::string, TaskTime> tasks;
    std::ofstream logFile;
    std::mutex logMutex;
    bool logFileInitialized = false;

    // 初始化日志文件
    void initializeLogFile();
};

extern TimeTracker& tracker;
#endif // TIME_TRACKER_H
