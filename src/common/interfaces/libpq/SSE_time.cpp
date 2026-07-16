#include "SSE_time.h"
#include <stdexcept>
#include <iostream>

TimeTracker& TimeTracker::getInstance() {
    static TimeTracker instance;
    return instance;
}

TimeTracker::TimeTracker() {}

TimeTracker::~TimeTracker() {
    std::lock_guard<std::mutex> guard(logMutex);
    if (logFile.is_open()) {
        logFile.close();
    }
}

void TimeTracker::initializeLogFile() {
    if (!logFileInitialized) {
        logFile.open("/opt/og/openGauss-server/sse_storage/sse_tpch_time.txt", std::ios::out | std::ios::app);
        if (!logFile.is_open()) {
            throw std::runtime_error("Failed to open default log file: default_log.txt");
        }
        logFileInitialized = true;
    }
}

void TimeTracker::setLogFile(const std::string& fileName) {
    std::lock_guard<std::mutex> guard(logMutex);
    if (logFile.is_open()) {
        logFile.close();
    }
    logFile.open(fileName, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        throw std::runtime_error("Failed to open log file: " + fileName);
    }
    logFileInitialized = true;
}

void TimeTracker::start(const std::string& taskName) {
    std::lock_guard<std::mutex> guard(logMutex);
    tasks[taskName] = {std::chrono::high_resolution_clock::now(), true};
}

void TimeTracker::stop(const std::string& taskName) {
    std::lock_guard<std::mutex> guard(logMutex);

    auto it = tasks.find(taskName);
    if (it == tasks.end() || !it->second.isActive) {
        throw std::runtime_error("Task not found or not active: " + taskName);
    }

    // 计算时间间隔
    auto endTime = std::chrono::high_resolution_clock::now();
    long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - it->second.startTime).count();

    // 确保日志文件已初始化
    initializeLogFile();

    // 写入日志
    logFile << "Task: " << taskName << ", Elapsed time: " << elapsed << " ms" << std::endl;
    logFile.flush();

    // 移除任务
    tasks.erase(it);
}
TimeTracker& tracker= TimeTracker::getInstance();