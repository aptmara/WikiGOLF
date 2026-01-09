#pragma once
/**
 * @file FontManager.h
 * @brief フォントファイルのロードと TextFormat キャッシュ管理
 */

#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "TextStyle.h"
#include "../core/Logger.h"

namespace graphics {

using Microsoft::WRL::ComPtr;

/// @brief フォント管理クラス
/// @details カスタムフォントのロード、TextFormat のキャッシュを担当
class FontManager {
public:
    /// @brief 初期化
    /// @param factory DirectWrite ファクトリ
    void Initialize(IDWriteFactory* factory) {
        m_factory = factory;
        m_formatCache.clear();
    }

    /// @brief 終了処理
    void Shutdown() {
        // ロード済みフォントを解除
        for (const auto& path : m_loadedFontPaths) {
            RemoveFontResourceExA(path.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr);
        }
        m_loadedFontPaths.clear();
        m_formatCache.clear();
        m_factory = nullptr;
    }

    /// @brief フォントファイルをシステムに登録
    /// @param fontName 登録名（UIText の fontFamily と一致させる）
    /// @param filePath フォントファイルパス（.otf / .ttf）
    /// @return 成功なら true
    bool LoadFont(const std::string& fontName, const std::string& filePath) {
        int result = AddFontResourceExA(filePath.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr);
        if (result > 0) {
            m_loadedFontPaths.push_back(filePath);
            m_fontNameToFamily[fontName] = fontName; // 通常は fontName == ファミリー名
            LOG_INFO("FontManager", "Loaded font: {} from {}", fontName, filePath);
            return true;
        } else {
            LOG_ERROR("FontManager", "Failed to load font: {} from {}", fontName, filePath);
            return false;
        }
    }

    /// @brief TextFormat を取得（キャッシュがあれば再利用）
    /// @param fontName フォント名
    /// @param size フォントサイズ
    /// @param align 水平アラインメント
    /// @return TextFormat へのポインタ（作成失敗時は nullptr）
    IDWriteTextFormat* GetFormat(const std::string& fontName, float size, TextAlign align) {
        if (!m_factory) return nullptr;

        // キャッシュキー作成
        std::string key = fontName + "_" + std::to_string(static_cast<int>(size)) + "_" + std::to_string(static_cast<int>(align));
        auto it = m_formatCache.find(key);
        if (it != m_formatCache.end()) {
            return it->second.Get();
        }

        // 新規作成
        std::wstring wFontName(fontName.begin(), fontName.end());
        ComPtr<IDWriteTextFormat> format;

        HRESULT hr = m_factory->CreateTextFormat(
            wFontName.c_str(),
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
            L"ja-JP",
            &format
        );

        if (FAILED(hr)) {
            // フォールバック: システムフォント
            LOG_ERROR("FontManager", "Failed to create TextFormat for '{}', falling back to 'Yu Gothic UI'", fontName);
            hr = m_factory->CreateTextFormat(
                L"Yu Gothic UI",
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                L"ja-JP",
                &format
            );
            if (FAILED(hr)) {
                LOG_ERROR("FontManager", "Fallback font also failed");
                return nullptr;
            }
        }

        // アラインメント設定
        switch (align) {
            case TextAlign::Left:
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                break;
            case TextAlign::Center:
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                break;
            case TextAlign::Right:
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                break;
        }

        m_formatCache[key] = format;
        return format.Get();
    }

private:
    IDWriteFactory* m_factory = nullptr;
    std::vector<std::string> m_loadedFontPaths;
    std::unordered_map<std::string, std::string> m_fontNameToFamily;
    std::unordered_map<std::string, ComPtr<IDWriteTextFormat>> m_formatCache;
};

} // namespace graphics
