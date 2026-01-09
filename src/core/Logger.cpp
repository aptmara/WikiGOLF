/**
 * @file Logger.cpp
 * @brief ログシステムの実装
 */

#include "Logger.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <windows.h> // OutputDebugString, SetConsoleTextAttribute

namespace core {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    Shutdown();
}

void Logger::Initialize(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;

    m_fileStream.open(filename, std::ios::out | std::ios::trunc);
    if (m_fileStream.is_open()) {
        m_initialized = true;
        
        // ヘッダー書き込み (localtime_s 使用)
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        localtime_s(&tm_buf, &in_time_t);

        m_fileStream << "=== Game Log Started at " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << " ===" << std::endl;
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream << "=== Game Log Ended ===" << std::endl;
        m_fileStream.close();
    }
    m_initialized = false;
}

void Logger::Log(LogLevel level, const char* category, const char* file, int line, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_s(&tm_buf, &in_time_t);

    // パスからファイル名のみ抽出
    std::string filename = file;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    // レベル文字列と色
    const char* levelStr = "";
    WORD color = 0;
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG"; color = FOREGROUND_INTENSITY; break; // Gray
        case LogLevel::Info:    levelStr = "INFO "; color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break; // White
        case LogLevel::Warning: levelStr = "WARN "; color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break; // Yellow
        case LogLevel::Error:   levelStr = "ERROR"; color = FOREGROUND_RED | FOREGROUND_INTENSITY; break; // Red
    }

    // フォーマット: [Time] [Level] [Category] Message (File:Line)
    std::stringstream ss;
    ss << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "] "
       << "[" << levelStr << "] "
       << "[" << category << "] "
       << message
       << " (" << filename << ":" << line << ")";

    std::string fullMessage = ss.str();

    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. ファイル出力
    if (m_initialized && m_fileStream.is_open()) {
        m_fileStream << fullMessage << std::endl;
        if (level == LogLevel::Error) m_fileStream.flush();
    }

    // 2. Visual Studio デバッグ出力
    std::string debugOutput = fullMessage + "\n";
    OutputDebugStringA(debugOutput.c_str());

    // 3. 標準出力 (コンソール)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, color);
        std::cout << fullMessage << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); // Reset
    }
}

} // namespace core
