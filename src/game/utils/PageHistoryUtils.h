#pragma once
/**
 * @file PageHistoryUtils.h
 * @brief WikiGolf のページ履歴操作ユーティリティです。
 */

#include <optional>
#include <string>
#include <vector>

namespace game::utils {

/**
 * @brief 1 つ前のページへ戻るために履歴を巻き戻します。
 * @param pathHistory 訪問ページ履歴です。
 * @return 戻り先ページ名。戻れない場合は std::nullopt です。
 * @details 現在ページと戻り先ページの履歴を一旦取り除き、呼び出し側で
 *          戻り先ページを再ロードした際に履歴へ再追加し直せる形へ整えます。
 */
std::optional<std::string>
ConsumePreviousPage(std::vector<std::string>& pathHistory);

} // namespace game::utils
