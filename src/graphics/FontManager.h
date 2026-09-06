#pragma once
/**
 * @file FontManager.h
 * @brief フォントファイルのロードと TextFormat キャッシュ管理
 */

#include <dwrite_3.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "TextStyle.h"
#include "../core/Logger.h"

namespace graphics {

using Microsoft::WRL::ComPtr;

/// @brief フォント管理クラス
/// @details カスタムフォントのロード、TextFormat のキャッシュを担当
///
/// AddFontResourceEx(FR_PRIVATE) による GDI 登録だけでは、
/// IDWriteFactory::CreateTextFormat がそのファミリー名を見つけられるとは
/// 限らない（見つからなくても CreateTextFormat 自体はエラーを返さず、
/// 描画時に静かに既定フォントへフォールバックするため気付きにくい）。
/// そのため同梱フォントは IDWriteFontSetBuilder で専用の
/// IDWriteFontCollection を組み立て、CreateTextFormat にそのコレクションを
/// 明示的に渡すことで確実に解決させる。
class FontManager {
    /// @brief LoadFont() に渡す複合ファミリー名 -> 実際の解決先の対応
    /// @details 同梱フォントのビルド済みコレクション内では、DirectWriteが
    ///          OS/2テーブルの usWidthClass/usWeightClass と一致する語
    ///          （"Condensed" "Medium" 等）をファミリー名から取り除くため、
    ///          複合名（"Barlow Condensed Medium"）ではファミリーが見つからない。
    struct BundledFamilyOverride {
        std::string baseFamily;
        DWRITE_FONT_WEIGHT weight;
        DWRITE_FONT_STRETCH stretch;
    };
    static const std::unordered_map<std::string, BundledFamilyOverride>& GetBundledFamilyOverrides() {
        static const std::unordered_map<std::string, BundledFamilyOverride> table = {
            {"Barlow Condensed",          {"Barlow", DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_CONDENSED}},
            {"Barlow Condensed Medium",   {"Barlow", DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STRETCH_CONDENSED}},
            {"Barlow Condensed SemiBold", {"Barlow", DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STRETCH_CONDENSED}},
            {"Barlow Condensed Black",    {"Barlow", DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STRETCH_CONDENSED}},
            {"Kiwi Maru Medium",          {"Kiwi Maru", DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STRETCH_NORMAL}},
        };
        return table;
    }

public:
    /// @brief 初期化
    /// @param factory DirectWrite ファクトリ
    void Initialize(IDWriteFactory* factory) {
        m_factory = factory;
        m_formatCache.clear();
        m_missingBundledFamilyWarnings.clear();
        m_factory5.Reset();
        if (factory) {
            factory->QueryInterface(__uuidof(IDWriteFactory5),
                                    reinterpret_cast<void**>(m_factory5.GetAddressOf()));
            if (!m_factory5) {
                LOG_WARN("FontManager",
                        "IDWriteFactory5 unavailable. Bundled fonts may fail to resolve by name.");
            }
        }
    }

    /// @brief 終了処理
    void Shutdown() {
        for (const auto& path : m_loadedFontPaths) {
            RemoveFontResourceExA(path.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr);
        }
        m_loadedFontPaths.clear();
        m_pendingFontFiles.clear();
        m_collection.Reset();
        m_formatCache.clear();
        m_missingBundledFamilyWarnings.clear();
        m_factory5.Reset();
        m_factory = nullptr;
    }

    /// @brief フォントファイルを登録
    /// @param fontName ログ表示用の名前（実際の解決はファイル内蔵のファミリー名で行う）
    /// @param filePath フォントファイルパス（.otf / .ttf）
    /// @return 成功なら true
    bool LoadFont(const std::string& fontName, const std::string& filePath) {
        // 旧経路（GDI）も一応登録しておく。害はないが、実際の解決には使わない。
        AddFontResourceExA(filePath.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr);
        m_loadedFontPaths.push_back(filePath);

        std::wstring wPath(filePath.begin(), filePath.end());
        m_pendingFontFiles.push_back(std::move(wPath));
        m_collectionDirty = true;
        m_missingBundledFamilyWarnings.clear();
        LOG_INFO("FontManager", "Queued bundled font: {} ({})", fontName, filePath);
        return true;
    }

    /// @brief TextFormat を取得（キャッシュがあれば再利用）
    /// @param fontName フォント名
    /// @param size フォントサイズ
    /// @param align 水平アラインメント
    /// @return TextFormat へのポインタ（作成失敗時は nullptr）
    IDWriteTextFormat* GetFormat(const std::string& fontName, float size, TextAlign align) {
        if (!m_factory) return nullptr;

        // キャッシュキー作成
        // floatサイズをintへ丸めると、僅かに異なるサイズが同一TextFormatとして
        // 誤って再利用されてしまう（=キャッシュヒット時の実際のレイアウトサイズと
        // フォーマットのサイズがズレる）ため、ビット完全一致のキーを用いる。
        uint32_t sizeBits = 0;
        std::memcpy(&sizeBits, &size, sizeof(sizeBits));
        std::string key = fontName + "_" + std::to_string(sizeBits) + "_" + std::to_string(static_cast<int>(align));
        auto it = m_formatCache.find(key);
        if (it != m_formatCache.end()) {
            return it->second.Get();
        }

        std::wstring wFontName(fontName.begin(), fontName.end());
        ComPtr<IDWriteTextFormat> format;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;

        // 同梱フォントの専用コレクションにこの名前があれば、そちらを明示指定する。
        // 見つからない場合は nullptr（システムコレクション）のままにする。
        IDWriteFontCollection* collectionToUse = nullptr;
        IDWriteFontCollection1* bundled = EnsureCollection();
        if (bundled) {
            BOOL exists = FALSE;
            UINT32 index = 0;
            if (SUCCEEDED(bundled->FindFamilyName(wFontName.c_str(), &index, &exists)) && exists) {
                collectionToUse = bundled;
            } else {
                // DirectWriteはOS/2テーブルの usWidthClass/usWeightClass と一致する
                // "Condensed"/"Medium" 等の語をファミリー名から取り除き、weight/stretch
                // 側の属性へ変換してしまう（例: "Barlow Condensed Medium" は
                // ファミリー"Barlow" + Weight=Medium + Stretch=Condensed になる）。
                // そのため既知の複合名は基底ファミリー名 + 明示的な weight/stretch で
                // 引き直す。
                const auto& overrides = GetBundledFamilyOverrides();
                auto ov = overrides.find(fontName);
                if (ov != overrides.end()) {
                    std::wstring wBase(ov->second.baseFamily.begin(), ov->second.baseFamily.end());
                    if (SUCCEEDED(bundled->FindFamilyName(wBase.c_str(), &index, &exists)) && exists) {
                        collectionToUse = bundled;
                        wFontName = wBase;
                        weight = ov->second.weight;
                        stretch = ov->second.stretch;
                    }
                }
                if (!collectionToUse && m_missingBundledFamilyWarnings.insert(fontName).second) {
                    LOG_WARN("FontManager",
                            "'{}' not found in bundled font collection. Using system collection.",
                            fontName);
                }
            }
        }

        HRESULT hr = m_factory->CreateTextFormat(
            wFontName.c_str(),
            collectionToUse,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            stretch,
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
    /// @brief 同梱フォントファイル群から IDWriteFontCollection を（未構築/変更時のみ）組み立てる
    IDWriteFontCollection1* EnsureCollection() {
        if (!m_factory5) return nullptr;
        if (m_collection && !m_collectionDirty) return m_collection.Get();
        if (m_pendingFontFiles.empty()) return nullptr;

        ComPtr<IDWriteFontSetBuilder1> builder;
        if (FAILED(m_factory5->CreateFontSetBuilder(&builder))) {
            LOG_ERROR("FontManager", "CreateFontSetBuilder failed");
            return nullptr;
        }

        int added = 0;
        for (const auto& path : m_pendingFontFiles) {
            ComPtr<IDWriteFontFile> file;
            if (SUCCEEDED(m_factory->CreateFontFileReference(path.c_str(), nullptr, &file))) {
                if (SUCCEEDED(builder->AddFontFile(file.Get()))) {
                    ++added;
                }
            } else {
                LOG_ERROR("FontManager", "CreateFontFileReference failed for bundled font file");
            }
        }
        if (added == 0) return nullptr;

        ComPtr<IDWriteFontSet> fontSet;
        if (FAILED(builder->CreateFontSet(&fontSet))) {
            LOG_ERROR("FontManager", "CreateFontSet failed");
            return nullptr;
        }

        m_collection.Reset();
        if (FAILED(m_factory5->CreateFontCollectionFromFontSet(fontSet.Get(), &m_collection))) {
            LOG_ERROR("FontManager", "CreateFontCollectionFromFontSet failed");
            return nullptr;
        }
        m_collectionDirty = false;
        LOG_INFO("FontManager", "Built bundled font collection with {} file(s)", added);
        return m_collection.Get();
    }

    IDWriteFactory* m_factory = nullptr;
    ComPtr<IDWriteFactory5> m_factory5;
    std::vector<std::string> m_loadedFontPaths;
    std::vector<std::wstring> m_pendingFontFiles;
    ComPtr<IDWriteFontCollection1> m_collection;
    bool m_collectionDirty = false;
    std::unordered_map<std::string, ComPtr<IDWriteTextFormat>> m_formatCache;
    std::unordered_set<std::string> m_missingBundledFamilyWarnings;
};

} // namespace graphics
