#pragma once

#include <string>
#include <string_view>

namespace onix {

// Assets do painel (ADR-0010): o stage publica só o sidecar .gz quando o build gera um, e o
// servidor o entrega sempre, sem negociar Accept-Encoding. Sem .gz (ex.: index.html pequeno),
// vale o arquivo original.
inline std::string selectAssetPath(std::string_view requestedPath, bool gzipExists) {
  std::string path(requestedPath);
  if (gzipExists) path += ".gz";
  return path;
}

inline const char* cacheControlForAsset(std::string_view path) noexcept {
  return path.substr(0, 8) == "/assets/" ? "public, max-age=31536000, immutable" : "no-cache";
}

}  // namespace onix
