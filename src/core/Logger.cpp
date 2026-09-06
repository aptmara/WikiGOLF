/**
 * @file Logger.cpp
 * @brief ログシステムの実装
 */

#include "Logger.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
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
    std::filesystem::path warningPath(filename);
    const std::string warningFilename =
        warningPath.stem().string() + "_warnings" +
        warningPath.extension().string();
    warningPath.replace_filename(warningFilename);
    m_warningFileStream.open(warningPath, std::ios::out | std::ios::trunc);

    std::filesystem::path perfPath(filename);
    const std::string perfFilename =
        perfPath.stem().string() + "_perf" +
        perfPath.extension().string();
    perfPath.replace_filename(perfFilename);
    m_perfFileStream.open(perfPath, std::ios::out | std::ios::trunc);

    m_initialized = m_fileStream.is_open() || m_warningFileStream.is_open() ||
                     m_perfFileStream.is_open();
    if (m_initialized) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        localtime_s(&tm_buf, &in_time_t);

        if (m_fileStream.is_open()) {
            m_fileStream << "=== Game Log Started at "
                         << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
                         << " ===\n";
        }
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream << "=== Game Log Ended ===\n";
        m_fileStream.close();
    }
    if (m_warningFileStream.is_open()) m_warningFileStream.close();
    if (m_perfFileStream.is_open()) m_perfFileStream.close();
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

    // "Perf" は毎秒フレーム統計を吐くため、本編ログに混ぜると他のイベントが
    // 埋もれてしまう（詳細な内訳は Profiler が別途CSVへも出力済みで冗長）。
    // 専用ファイルへ分離する。
    const bool isPerf = std::strcmp(category, "Perf") == 0;

    // ファイル出力処理
    if (m_initialized && m_fileStream.is_open() && !isPerf) {
        m_fileStream << fullMessage << '\n';
        if (level == LogLevel::Error) m_fileStream.flush();
    }
    if (m_initialized && m_perfFileStream.is_open() && isPerf) {
        m_perfFileStream << fullMessage << '\n';
    }
    if (m_initialized && m_warningFileStream.is_open() &&
        (level == LogLevel::Warning || level == LogLevel::Error)) {
        m_warningFileStream << fullMessage << std::endl;
        m_warningFileStream.flush();
    }

    // デバッガ/コンソール出力は開発時だけに限定する。Releaseではファイル出力を
    // 維持しつつ、OutputDebugStringと標準出力の同期コストを避ける。
#ifdef _DEBUG
    std::string debugOutput = fullMessage + "\n";
    OutputDebugStringA(debugOutput.c_str());

    // 標準出力（コンソール）処理
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, color);
        std::cout << fullMessage << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); // Reset
    }
#endif
}

} // namespace core
