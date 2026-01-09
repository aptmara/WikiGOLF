#pragma once
/**
 * @file Logger.h
 * @brief 多機能ログシステム
 */

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <format> // C++20

namespace core {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// @brief ログシステムの初期化
    /// @param filename 出力するログファイル名
    void Initialize(const std::string& filename = "game.log");

    /// @brief 終了処理
    void Shutdown();

    /// @brief ログ出力（内部実装）
    void Log(LogLevel level, const char* category, const char* file, int line, const std::string& message);

    /// @brief フォーマット付きログ出力ヘルパー
    template<typename... Args>
    void LogFmt(LogLevel level, const char* category, const char* file, int line, std::format_string<Args...> fmt, Args&&... args) {
        try {
            std::string message = std::format(fmt, std::forward<Args>(args)...);
            Log(level, category, file, line, message);
        } catch (...) {
            Log(LogLevel::Error, "Logger", file, line, "Format error in log message");
        }
    }

private:
    Logger() = default;
    ~Logger();

    std::ofstream m_fileStream;
    std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace core

//==============================================================================
// マクロ定義 (呼び出しを簡略化)
//==============================================================================

#ifdef _DEBUG
    #define LOG_DEBUG(Category, Fmt, ...) core::Logger::Instance().LogFmt(core::LogLevel::Debug, Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
#else
    #define LOG_DEBUG(Category, Fmt, ...) ((void)0)
#endif

#define LOG_INFO(Category, Fmt, ...)  core::Logger::Instance().LogFmt(core::LogLevel::Info,  Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
#define LOG_WARN(Category, Fmt, ...)  core::Logger::Instance().LogFmt(core::LogLevel::Warning, Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
#define LOG_ERROR(Category, Fmt, ...) core::Logger::Instance().LogFmt(core::LogLevel::Error, Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
