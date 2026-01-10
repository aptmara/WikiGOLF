#include "ObjLoader.h"
#include "../core/Logger.h"
#include "TangentGenerator.h"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace graphics {

using namespace DirectX;

namespace {

//==============================================================================
// ヘルパー構造体
//==============================================================================

struct ObjVertexIndex {
  int v = 0;
  int vt = 0;
  int vn = 0;

  bool operator==(const ObjVertexIndex &other) const {
    return v == other.v && vt == other.vt && vn == other.vn;
  }
};

struct ObjVertexIndexHash {
  size_t operator()(const ObjVertexIndex &k) const {
    size_t h1 = std::hash<int>()(k.v);
    size_t h2 = std::hash<int>()(k.vt);
    size_t h3 = std::hash<int>()(k.vn);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

//==============================================================================
// Modern Fast Obj Parser (C++17/20)
//==============================================================================

class FastObjParser {
public:
  FastObjParser(const std::string &path) : m_path(path) {}

  bool Parse(std::vector<Vertex> &outVertices,
             std::vector<uint32_t> &outIndices) {
    if (!ReadFile())
      return false;

    std::string_view sv(m_buffer);
    m_cursor = 0;
    m_line = 1;

    // 予約 (ヒューリスティック)
    m_positions.reserve(4096);
    m_texCoords.reserve(4096);
    m_normals.reserve(4096);
    outVertices.reserve(4096);
    outIndices.reserve(8192);

    while (m_cursor < sv.size()) {
      SkipWhitespace(sv);
      if (m_cursor >= sv.size())
        break;

      if (Peek(sv) == '#') {
        SkipLine(sv);
        continue;
      }

      // トークン読み取り
      std::string_view token = ReadToken(sv);
      if (token == "v") {
        ParsePosition(sv);
      } else if (token == "vt") {
        ParseTexCoord(sv);
      } else if (token == "vn") {
        ParseNormal(sv);
      } else if (token == "f") {
        ParseFace(sv, outVertices, outIndices);
      } else {
        // 不明な・あるいは対応不要な行 (g, o, s, mtllib, usemtl etc)
        SkipLine(sv);
      }
    }

    return true;
  }

private:
  std::string m_path;
  std::string m_buffer; // string_viewのオーナー
  size_t m_cursor = 0;
  int m_line = 1;

  std::vector<XMFLOAT3> m_positions;
  std::vector<XMFLOAT2> m_texCoords;
  std::vector<XMFLOAT3> m_normals;

  std::unordered_map<ObjVertexIndex, uint32_t, ObjVertexIndexHash> m_indexMap;

  bool ReadFile() {
    // バイナリモードで開き、サイズを取得して一括読み込み
    std::ifstream file(m_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      LOG_ERROR("ObjLoader", "ファイルオープン失敗: {}", m_path);
      return false;
    }

    auto size = file.tellg();
    if (size <= 0)
      return true; // 空ファイルは成功扱い（何もしない）

    m_buffer.resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(m_buffer.data(), size);
    return true;
  }

  char Peek(std::string_view sv) const {
    if (m_cursor < sv.size())
      return sv[m_cursor];
    return '\0';
  }

  void SkipWhitespace(std::string_view sv) {
    while (m_cursor < sv.size()) {
      char c = sv[m_cursor];
      if (c == ' ' || c == '\t' || c == '\r') {
        m_cursor++;
      } else if (c == '\n') {
        m_cursor++;
        m_line++;
      } else {
        break;
      }
    }
  }

  void SkipLine(std::string_view sv) {
    while (m_cursor < sv.size()) {
      char c = sv[m_cursor];
      m_cursor++;
      if (c == '\n') {
        m_line++;
        break;
      }
    }
  }

  std::string_view ReadToken(std::string_view sv) {
    SkipWhitespace(sv);
    size_t start = m_cursor;
    while (m_cursor < sv.size()) {
      char c = sv[m_cursor];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        break;
      }
      m_cursor++;
    }
    return sv.substr(start, m_cursor - start);
  }

  float ParseFloat(std::string_view sv) {
    SkipWhitespace(sv);
    if (m_cursor >= sv.size())
      return 0.0f;

    // 次の区切りまでを探す（簡易的）
    // std::from_charsは終端が必要、string_viewは必ずしもnull終端ではないので
    // data() + size() を渡す
    const char *start = sv.data() + m_cursor;
    const char *end = sv.data() + sv.size();
    float val = 0.0f;

    // 空白までの範囲を特定（from_charsは空白で止まらない場合があるため）
    // ただし from_chars は数値として認識できる最長マッチを行うので、
    // 次の文字が数値構成文字でなければ止まる。
    auto result = std::from_chars(start, end, val);
    if (result.ec == std::errc()) {
      m_cursor += (result.ptr - start);
    } else {
      // エラー時は0.0fとし、カーソルを進めない（あるいは次のトークンまで飛ばす）
      // ここでは次の空白までスキップする等の回復処理を入れる
      ReadToken(sv); // スキップ
    }
    return val;
  }

  int ParseInt(std::string_view sv) {
    // SkipWhitespaceは呼び出し元で行われている前提もあるが、ここでも呼んでおく
    // ただし / 区切りの中などでは呼ばないほうが良い場合も。
    // ここでは汎用的な数値取得として
    const char *start = sv.data() + m_cursor;
    const char *end = sv.data() + sv.size();
    int val = 0;
    auto result = std::from_chars(start, end, val);
    if (result.ec == std::errc()) {
      m_cursor += (result.ptr - start);
    }
    return val;
  }

  void ParsePosition(std::string_view sv) {
    float x = ParseFloat(sv);
    float y = ParseFloat(sv);
    float z = ParseFloat(sv);
    // Z反転 (RH -> LH)
    m_positions.push_back({x, y, -z});
  }

  void ParseTexCoord(std::string_view sv) {
    float u = ParseFloat(sv);
    float v = ParseFloat(sv);
    // V反転 (OpenGL -> DirectX)
    m_texCoords.push_back({u, 1.0f - v});
  }

  void ParseNormal(std::string_view sv) {
    float x = ParseFloat(sv);
    float y = ParseFloat(sv);
    float z = ParseFloat(sv);
    // Z反転 (RH -> LH)
    m_normals.push_back({x, y, -z});
  }

  void ParseFace(std::string_view sv, std::vector<Vertex> &outVertices,
                 std::vector<uint32_t> &outIndices) {
    std::vector<ObjVertexIndex> faceIndices;
    faceIndices.reserve(4);

    while (m_cursor < sv.size()) {
      // 行末チェック
      char c = Peek(sv);
      if (c == '\n' || c == '\r' || c == '\0')
        break;

      // 行内空白スキップ
      if (c == ' ' || c == '\t') {
        m_cursor++;
        continue;
      }

      // 数値読み取り開始
      ObjVertexIndex idx;
      idx.v = ParseInt(sv);

      // /vt/vn パターン確認
      if (Peek(sv) == '/') {
        m_cursor++; // skip first /
        if (Peek(sv) == '/') {
          // v//vn
          m_cursor++; // skip second /
          idx.vn = ParseInt(sv);
        } else {
          // v/vt...
          idx.vt = ParseInt(sv);
          if (Peek(sv) == '/') {
            // v/vt/vn
            m_cursor++;
            idx.vn = ParseInt(sv);
          }
        }
      }

      // インデックス補正 (1-based -> 0-based, 負数は末尾からのオフセット)
      idx.v = FixIndex(idx.v, m_positions.size());
      idx.vt = FixIndex(idx.vt, m_texCoords.size());
      idx.vn = FixIndex(idx.vn, m_normals.size());

      faceIndices.push_back(idx);
    }

    // 三角形分割 (Fan)
    if (faceIndices.size() >= 3) {
      for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
        AddVertex(faceIndices[0], outVertices, outIndices);
        AddVertex(faceIndices[i + 1], outVertices, outIndices);
        AddVertex(faceIndices[i], outVertices, outIndices);
      }
    }
  }

  int FixIndex(int idx, size_t size) {
    if (idx > 0)
      return idx - 1;
    if (idx < 0)
      return static_cast<int>(size) + idx;
    return -1;
  }

  void AddVertex(const ObjVertexIndex &idx, std::vector<Vertex> &outVertices,
                 std::vector<uint32_t> &outIndices) {
    if (m_indexMap.find(idx) == m_indexMap.end()) {
      Vertex v = {};
      if (idx.v >= 0 && idx.v < (int)m_positions.size())
        v.position = m_positions[idx.v];
      if (idx.vt >= 0 && idx.vt < (int)m_texCoords.size())
        v.texCoord = m_texCoords[idx.vt];
      if (idx.vn >= 0 && idx.vn < (int)m_normals.size())
        v.normal = m_normals[idx.vn];
      v.color = {1.0f, 1.0f, 1.0f, 1.0f};

      uint32_t newIndex = static_cast<uint32_t>(outVertices.size());
      m_indexMap[idx] = newIndex;
      outVertices.push_back(v);
    }
    outIndices.push_back(m_indexMap[idx]);
  }
};

} // namespace

bool ObjLoader::Load(const std::string &path, std::vector<Vertex> &outVertices,
                     std::vector<uint32_t> &outIndices) {
  FastObjParser parser(path);
  if (!parser.Parse(outVertices, outIndices)) {
    return false;
  }

  ComputeTangents(outVertices, outIndices);

  LOG_INFO("ObjLoader", "ロード完了: {} (Vertices: {}, Indices: {})",
           path.c_str(), outVertices.size(), outIndices.size());
  return true;
}

} // namespace graphics
