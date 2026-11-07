#pragma once
/**
 * @file ShaderPrecompiled.h
 * @brief ビルド時にコンパイルしておいたシェーダー（.cso）の扱い
 * @details 起動やロードのたびに HLSL をコンパイルすると合計 2 秒ほどかかる。
 *          ビルド時に ShaderPrecompile が、ゲームと同じ D3DCompileFromFile と同じフラグで
 *          各 *VS.hlsl / *PS.hlsl / *CS.hlsl のエントリ main をコンパイルし、同じフォルダへ
 *          <名前>.cso を書く。最後に、そのときのソース（.hlsl / .hlsli）全体のハッシュを
 *          kShaderSourceHashFile に書く。
 *          ゲームはフォルダ内のソースのハッシュがこれと一致するときだけ .cso を使い、
 *          一致しなければ従来どおりコンパイルする。ファイルの更新時刻は配布の途中で
 *          変わり得るので使わない。
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace graphics::shader_precompiled {

/** @brief ソースのハッシュを書くファイル名（シェーダーと同じフォルダ） */
inline constexpr const wchar_t *kShaderSourceHashFile = L"precompiled_shaders.hash";

/** @brief ファイル名から決まるプロファイル（*VS.hlsl / *PS.hlsl / *CS.hlsl 以外は空） */
inline const char *ProfileFor(const std::filesystem::path &source) {
  const std::wstring stem = source.stem().wstring();
  if (source.extension() != L".hlsl" || stem.size() < 2) return nullptr;
  const std::wstring suffix = stem.substr(stem.size() - 2);
  if (suffix == L"VS") return "vs_5_0";
  if (suffix == L"PS") return "ps_5_0";
  if (suffix == L"CS") return "cs_5_0";
  return nullptr;
}

/** @brief ソースに対応する .cso のパス */
inline std::filesystem::path CompiledPathFor(const std::filesystem::path &source) {
  std::filesystem::path out = source;
  out.replace_extension(L".cso");
  return out;
}

/** @brief フォルダ内の .hlsl / .hlsli 全体のハッシュ（名前順に、名前と中身を混ぜる） */
inline std::optional<uint64_t> HashShaderSources(const std::filesystem::path &dir) {
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const auto ext = entry.path().extension();
    if (ext == L".hlsl" || ext == L".hlsli") files.push_back(entry.path());
  }
  if (ec || files.empty()) return std::nullopt;
  std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
    return a.filename().wstring() < b.filename().wstring();
  });
  uint64_t hash = 1469598103934665603ull;
  auto mix = [&hash](const void *data, size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i) {
      hash ^= bytes[i];
      hash *= 1099511628211ull;
    }
  };
  std::vector<char> buffer;
  for (const auto &file : files) {
    const std::wstring name = file.filename().wstring();
    mix(name.data(), name.size() * sizeof(wchar_t));
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    buffer.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    const uint64_t size = buffer.size();
    mix(&size, sizeof(size));
    mix(buffer.data(), buffer.size());
  }
  return hash;
}

inline std::string FormatHash(uint64_t hash) {
  char text[17] = {};
  std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
  return text;
}

/** @brief フォルダの .cso が今のソースから作られたものか（結果はフォルダごとに覚える） */
inline bool IsCurrent(const std::filesystem::path &dir) {
  static std::mutex mutex;
  static std::map<std::wstring, bool> known;
  const std::wstring key = dir.lexically_normal().wstring();
  std::lock_guard<std::mutex> lock(mutex);
  if (auto it = known.find(key); it != known.end()) return it->second;

  bool current = false;
  std::ifstream in(dir / kShaderSourceHashFile);
  std::string recorded;
  if (in && std::getline(in, recorded)) {
    const auto actual = HashShaderSources(dir);
    current = actual && FormatHash(*actual) == recorded;
  }
  known.emplace(key, current);
  return current;
}

/** @brief 使える .cso があれば中身を返す */
inline std::optional<std::vector<char>> Load(const std::filesystem::path &source,
                                             const std::string &entry) {
  if (entry != "main" || !ProfileFor(source)) return std::nullopt;
  const std::filesystem::path dir =
      source.has_parent_path() ? source.parent_path() : std::filesystem::path(L".");
  if (!IsCurrent(dir)) return std::nullopt;
  std::ifstream in(CompiledPathFor(source), std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<char> bytes{std::istreambuf_iterator<char>(in),
                          std::istreambuf_iterator<char>()};
  if (bytes.empty()) return std::nullopt;
  return bytes;
}

} // namespace graphics::shader_precompiled
