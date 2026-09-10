#include "PlayFabClient.h"
#include "PlayFabClientJson.h"
#include "PlayFabRules.h"
#include "WikiClientJson.h"
#include "../../core/Logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace game::systems {

namespace {
constexpr const wchar_t *kPlayFabHost = L"16F5F8.playfabapi.com";
constexpr const char *kPlayFabTitleId = "16F5F8";
constexpr const char *kProfilePath = "save_playfab_profile.txt";

std::string EscapeJson(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
    case '\\': escaped += "\\\\"; break;
    case '"': escaped += "\\\""; break;
    case '\n': escaped += "\\n"; break;
    case '\r': escaped += "\\r"; break;
    case '\t': escaped += "\\t"; break;
    default: escaped += character; break;
    }
  }
  return escaped;
}

std::string GenerateCustomId() {
  std::random_device randomDevice;
  std::mt19937_64 random(randomDevice());
  std::ostringstream value;
  value << "wikigolf-" << std::hex << std::setfill('0') << std::setw(16)
        << random() << std::setw(16) << random();
  return value.str();
}

std::string ExtractErrorMessage(const std::string &response) {
  std::string message;
  size_t next = 0;
  if (wiki_json::ExtractStringField(response, "\"errorMessage\":\"", 0,
                                    response.size(), message, next)) {
    return message;
  }
  return "PlayFab request failed";
}
} // namespace

PlayFabProfile PlayFabClient::LoadOrCreateProfile() {
  PlayFabProfile profile;
  {
    std::ifstream input(kProfilePath, std::ios::binary);
    if (input) {
      std::getline(input, profile.customId);
      std::getline(input, profile.displayName);
    }
  }
  if (!profile.customId.empty()) {
    return profile;
  }

  profile.customId = GenerateCustomId();
  std::ofstream output(kProfilePath, std::ios::binary | std::ios::trunc);
  if (output) {
    output << profile.customId << '\n';
  }
  return profile;
}

bool PlayFabClient::SaveDisplayName(const std::string &displayName) {
  PlayFabProfile profile = LoadOrCreateProfile();
  profile.displayName = displayName;
  std::ofstream output(kProfilePath, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output << profile.customId << '\n' << profile.displayName << '\n';
  return true;
}

bool PlayFabClient::HasDisplayName() {
  return !LoadOrCreateProfile().displayName.empty();
}

PlayFabResult PlayFabClient::Login() {
  if (!m_sessionTicket.empty() && !m_entityToken.empty()) {
    return {true, {}};
  }
  m_profile = LoadOrCreateProfile();
  int statusCode = 0;
  const std::string body =
      "{\"TitleId\":\"" + std::string(kPlayFabTitleId) +
      "\",\"CustomId\":\"" + EscapeJson(m_profile.customId) +
      "\",\"CreateAccount\":true}";
  const std::string response =
      Post(L"/Client/LoginWithCustomID", body, Authorization::None,
           statusCode);
  if (statusCode != 200) {
    return {false, ExtractErrorMessage(response)};
  }

  size_t next = 0;
  if (!wiki_json::ExtractStringField(response, "\"SessionTicket\":\"", 0,
                                     response.size(), m_sessionTicket, next)) {
    return {false, "PlayFab login response did not contain a session ticket"};
  }
  if (!wiki_json::ExtractStringField(response, "\"EntityToken\":\"", 0,
                                     response.size(), m_entityToken, next)) {
    return {false, "PlayFab login response did not contain an entity token"};
  }
  return {true, {}};
}

PlayFabResult PlayFabClient::UpdateDisplayName(const std::string &displayName) {
  PlayFabResult login = Login();
  if (!login.success) {
    return login;
  }
  int statusCode = 0;
  const std::string body =
      "{\"DisplayName\":\"" + EscapeJson(displayName) + "\"}";
  const std::string response = Post(L"/Client/UpdateUserTitleDisplayName", body,
                                    Authorization::SessionTicket, statusCode);
  if (statusCode != 200) {
    return {false, ExtractErrorMessage(response)};
  }
  if (!SaveDisplayName(displayName)) {
    return {false, "Failed to save the local PlayFab profile"};
  }
  return {true, {}};
}

PlayFabResult PlayFabClient::SubmitDailyResult(int strokes, int clearTimeMs) {
  PlayFabResult login = Login();
  if (!login.success) {
    return login;
  }
  int statusCode = 0;
  const std::string body =
      "{\"Statistics\":[{\"Name\":\"" +
      std::string(kStrokesStatistic) + "\",\"Scores\":[\"" +
      std::to_string(strokes) + "\"]},{\"Name\":\"" +
      std::string(kClearTimeStatistic) + "\",\"Scores\":[\"" +
      std::to_string(clearTimeMs) + "\"]}]}";
  const std::string response =
      Post(L"/Statistic/UpdateStatistics", body, Authorization::EntityToken,
           statusCode);
  if (statusCode != 200) {
    return {false, ExtractErrorMessage(response)};
  }
  return {true, {}};
}

PlayFabLeaderboardResult
PlayFabClient::FetchLeaderboard(const std::string &statisticName,
                                int maxResults) {
  PlayFabLeaderboardResult result;
  PlayFabResult login = Login();
  if (!login.success) {
    result.errorMessage = login.errorMessage;
    return result;
  }
  int statusCode = 0;
  const std::string body =
      "{\"LeaderboardName\":\"" + EscapeJson(statisticName) +
      "\",\"StartingPosition\":1,\"PageSize\":" +
      std::to_string(std::clamp(maxResults, 1, 100)) + "}";
  const std::string response = Post(L"/Leaderboard/GetLeaderboard", body,
                                    Authorization::EntityToken, statusCode);
  if (statusCode != 200) {
    result.errorMessage = ExtractErrorMessage(response);
    return result;
  }

  if (!ParsePlayFabLeaderboard(response, result.entries)) {
    result.errorMessage = "PlayFab leaderboard response was invalid";
    return result;
  }
  result.success = true;
  return result;
}

std::string PlayFabClient::Post(const std::wstring &path,
                                const std::string &body,
                                Authorization authorization,
                                int &statusCode) {
  statusCode = 0;
  HINTERNET session = WinHttpOpen(
      L"WikiGOLF/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    return {};
  }
  WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);
  HINTERNET connection = WinHttpConnect(session, kPlayFabHost,
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = connection
                          ? WinHttpOpenRequest(
                                connection, L"POST", path.c_str(), nullptr,
                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                WINHTTP_FLAG_SECURE)
                          : nullptr;
  if (!request) {
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return {};
  }

  std::wstring headers = L"Content-Type: application/json\r\nX-PlayFabSDK: WikiGOLF-1.0";
  if (authorization == Authorization::SessionTicket) {
    headers += L"\r\nX-Authorization: ";
    headers.append(m_sessionTicket.begin(), m_sessionTicket.end());
  } else if (authorization == Authorization::EntityToken) {
    headers += L"\r\nX-EntityToken: ";
    headers.append(m_entityToken.begin(), m_entityToken.end());
  }
  const bool sent = WinHttpSendRequest(
      request, headers.c_str(), static_cast<DWORD>(-1),
      const_cast<char *>(body.data()), static_cast<DWORD>(body.size()),
      static_cast<DWORD>(body.size()), 0) != FALSE;
  if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return {};
  }

  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  WinHttpQueryHeaders(request,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                      WINHTTP_NO_HEADER_INDEX);
  statusCode = static_cast<int>(status);

  std::string response;
  DWORD available = 0;
  while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
    std::vector<char> buffer(available);
    DWORD downloaded = 0;
    if (!WinHttpReadData(request, buffer.data(), available, &downloaded)) {
      response.clear();
      break;
    }
    response.append(buffer.data(), downloaded);
  }
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return response;
}

} // namespace game::systems
