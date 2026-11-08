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
#include <atomic>
#include <vector>
#include "LogPrivacy.h"
#ifdef WIKIGOLF_DEBUG_TOOLS
#include <deque>
#include <vector>
#endif

namespace core {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

#ifdef WIKIGOLF_DEBUG_TOOLS
struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string text;
};
#endif

class Logger {
public:
    static Logger& Instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief ログシステムの初期化
     * @param filename 出力するログファイル名
*/
    void Initialize(const std::string& filename = "game.log");

    /** @brief 終了処理*/
    void Shutdown();

    /** @brief 診断情報を返す関数。中でログを出してはいけない*/
    using DiagnosticsProvider = std::string (*)();

    /**
     * @brief 最初の警告・エラーの前に書く診断情報の取得元を設定する
     * @details WIKIGOLF_LOG_WARNINGS_ONLY（製品版・体験版）のときだけ使われる
*/
    void SetDiagnosticsProvider(DiagnosticsProvider provider);

    /**
     * @brief ログから伏せる個人情報を設定する
     * @details WIKIGOLF_LOG_WARNINGS_ONLY（製品版・体験版）のときだけ使われる。
     *          診断情報とメッセージの両方に適用する
*/
    void SetPrivacyReplacements(std::vector<PrivacyReplacement> replacements);

    /** @brief ログ出力（内部実装）*/
    void Log(LogLevel level, const char* category, const char* file, int line, const std::string& message);

#ifdef WIKIGOLF_DEBUG_TOOLS
    std::vector<LogEntry> GetRecentEntries();
    void ClearRecentEntries();
#endif

    /** @brief フォーマット付きログ出力ヘルパー*/
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
    std::ofstream m_warningFileStream;
    std::ofstream m_perfFileStream; // "Perf" カテゴリ専用（毎秒出力され本編ログを埋めるため分離）
#ifdef WIKIGOLF_LOG_WARNINGS_ONLY
    // 警告・エラーだけを出すモード。何も起きなければファイルを作らないよう、最初の警告で開く
    std::string m_warningFilePath;
    std::atomic<bool> m_diagnosticsWritten = false;
#endif
    DiagnosticsProvider m_diagnosticsProvider = nullptr;
    std::vector<PrivacyReplacement> m_privacyReplacements;
    std::mutex m_mutex;
    bool m_initialized = false;
#ifdef WIKIGOLF_DEBUG_TOOLS
    static constexpr std::size_t kRecentEntryLimit = 1000;
    std::deque<LogEntry> m_recentEntries;
#endif
};

} // namespace core

//==============================================================================
// マクロ定義 (呼び出しを簡略化)
//==============================================================================

#ifdef WIKIGOLF_DISABLE_LOGGING
    #define LOG_DEBUG(Category, Fmt, ...) ((void)0)
    #define LOG_INFO(Category, Fmt, ...)  ((void)0)
    #define LOG_WARN(Category, Fmt, ...)  ((void)0)
    #define LOG_ERROR(Category, Fmt, ...) ((void)0)
#elif defined(_DEBUG)
    #define LOG_DEBUG(Category, Fmt, ...) core::Logger::Instance().LogFmt(core::LogLevel::Debug, Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
#else
    #define LOG_DEBUG(Category, Fmt, ...) ((void)0)
#endif

#ifndef WIKIGOLF_DISABLE_LOGGING
#ifdef WIKIGOLF_LOG_WARNINGS_ONLY
    // 製品版・体験版: 情報ログは出さず、警告とエラーだけを残す
    #define LOG_INFO(Category, Fmt, ...)  ((void)0)
#else
    #define LOG_INFO(Category, Fmt, ...)  core::Logger::Instance().LogFmt(core::LogLevel::Info,  Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
#endif
    #define LOG_WARN(Category, Fmt, ...)  core::Logger::Instance().LogFmt(core::LogLevel::Warning, Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
    #define LOG_ERROR(Category, Fmt, ...) core::Logger::Instance().LogFmt(core::LogLevel::Error, Category, __FILE__, __LINE__, Fmt, __VA_ARGS__)
#endif
