/**
 * @file tool_shader_precompile.cpp
 * @brief ビルド時にシェーダーをコンパイルして .cso を書く（ビルド後に自動で実行される）
 * @details 使い方: tool_shader_precompile <シェーダーのフォルダ>
 *          ゲーム（Shader.cpp）と同じ D3DCompileFromFile・同じフラグ（Release の 0）で
 *          *VS.hlsl / *PS.hlsl / *CS.hlsl のエントリ main をコンパイルする。詳しくは ShaderPrecompiled.h。
 *          ソースが前回と同じなら何もしない。
 */

#include "src/graphics/ShaderPrecompiled.h"

#include <atomic>
#include <d3dcompiler.h>
#include <iostream>
#include <thread>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace fs = std::filesystem;
namespace sp = graphics::shader_precompiled;
using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t **argv) {
  if (argc != 2) {
    std::wcerr << L"usage: tool_shader_precompile <shader dir>\n";
    return 2;
  }
  const fs::path dir = argv[1];
  const auto hash = sp::HashShaderSources(dir);
  if (!hash) {
    std::wcerr << L"no shader sources in " << dir.wstring() << L"\n";
    return 1;
  }
  const fs::path hashFile = dir / sp::kShaderSourceHashFile;

  std::vector<fs::path> sources;
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.is_regular_file() && sp::ProfileFor(entry.path())) {
      sources.push_back(entry.path());
    }
  }

  {
    std::ifstream in(hashFile);
    std::string recorded;
    bool allPresent = true;
    for (const auto &source : sources) {
      allPresent = allPresent && fs::exists(sp::CompiledPathFor(source));
    }
    if (in && std::getline(in, recorded) && recorded == sp::FormatHash(*hash) &&
        allPresent) {
      std::wcout << L"shaders are up to date (" << sources.size() << L")\n";
      return 0;
    }
  }

  // 作り直す間にゲームが古い .cso を使わないよう、先にハッシュを消す。
  std::error_code ec;
  fs::remove(hashFile, ec);

  std::atomic<size_t> next{0};
  std::atomic<int> failures{0};
  std::mutex outputMutex;
  auto worker = [&]() {
    for (size_t i = next++; i < sources.size(); i = next++) {
      const fs::path &source = sources[i];
      ComPtr<ID3DBlob> blob;
      ComPtr<ID3DBlob> errors;
      const HRESULT hr = D3DCompileFromFile(
          source.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
          sp::ProfileFor(source), 0, 0, &blob, &errors);
      if (FAILED(hr) || !blob) {
        // コンパイルできないシェーダーはゲームでも従来どおり失敗させる（.cso を作らない）。
        std::lock_guard<std::mutex> lock(outputMutex);
        std::wcerr << L"failed: " << source.filename().wstring() << L"\n";
        if (errors) {
          std::cerr << static_cast<const char *>(errors->GetBufferPointer()) << "\n";
        }
        fs::remove(sp::CompiledPathFor(source), ec);
        ++failures;
        continue;
      }
      std::ofstream out(sp::CompiledPathFor(source), std::ios::binary | std::ios::trunc);
      out.write(static_cast<const char *>(blob->GetBufferPointer()),
                static_cast<std::streamsize>(blob->GetBufferSize()));
      if (!out) {
        ++failures;
      }
    }
  };
  const unsigned threadCount =
      std::max(1u, std::min<unsigned>(std::thread::hardware_concurrency(),
                                      static_cast<unsigned>(sources.size())));
  std::vector<std::thread> threads;
  for (unsigned t = 0; t < threadCount; ++t) threads.emplace_back(worker);
  for (auto &thread : threads) thread.join();

  if (failures > 0) {
    // ビルドは止めない（ゲームは従来どおり実行時にコンパイルを試みる）。
    std::wcerr << failures.load() << L" shader(s) failed; leaving runtime compilation on\n";
    return 0;
  }
  std::ofstream(hashFile, std::ios::trunc) << sp::FormatHash(*hash) << "\n";
  std::wcout << L"precompiled " << sources.size() << L" shaders\n";
  return 0;
}
