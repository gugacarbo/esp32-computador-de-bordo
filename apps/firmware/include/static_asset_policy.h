#pragma once

#include <string>
#include <string_view>

namespace onix {

namespace static_asset_detail {

inline std::string_view trim(std::string_view value) noexcept {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  return value;
}

inline bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    char a = left[i];
    char b = right[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

inline bool positiveQuality(std::string_view value) noexcept {
  value = trim(value);
  if (value == "1") return true;
  if (value == "0" || value.size() < 3 || value[1] != '.') return false;

  const bool startsWithOne = value[0] == '1';
  if (!startsWithOne && value[0] != '0') return false;
  if (value.size() > 5) return false;  // RFC qvalue permits up to three decimals.

  bool nonZero = false;
  for (std::size_t i = 2; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    if (value[i] != '0') nonZero = true;
  }
  return startsWithOne ? !nonZero : nonZero;
}

}  // namespace static_asset_detail

inline bool acceptsGzipEncoding(std::string_view acceptEncoding) noexcept {
  while (!acceptEncoding.empty()) {
    const std::size_t comma = acceptEncoding.find(',');
    const std::string_view item = static_asset_detail::trim(acceptEncoding.substr(0, comma));
    const std::size_t semicolon = item.find(';');
    const std::string_view coding = static_asset_detail::trim(item.substr(0, semicolon));
    if (static_asset_detail::equalsIgnoreCase(coding, "gzip")) {
      if (semicolon == std::string_view::npos) return true;

      std::string_view parameters = item.substr(semicolon + 1);
      while (!parameters.empty()) {
        const std::size_t next = parameters.find(';');
        const std::string_view parameter = static_asset_detail::trim(parameters.substr(0, next));
        const std::size_t equals = parameter.find('=');
        if (equals != std::string_view::npos &&
            static_asset_detail::equalsIgnoreCase(
                static_asset_detail::trim(parameter.substr(0, equals)), "q")) {
          return static_asset_detail::positiveQuality(parameter.substr(equals + 1));
        }
        if (next == std::string_view::npos) break;
        parameters.remove_prefix(next + 1);
      }
      return true;
    }
    if (comma == std::string_view::npos) break;
    acceptEncoding.remove_prefix(comma + 1);
  }
  return false;
}

inline std::string selectAssetPath(std::string_view requestedPath, bool gzipAccepted,
                                   bool gzipExists) {
  std::string path(requestedPath);
  if (gzipAccepted && gzipExists) path += ".gz";
  return path;
}

inline const char* cacheControlForAsset(std::string_view path) noexcept {
  return path.substr(0, 8) == "/assets/" ? "public, max-age=31536000, immutable" : "no-cache";
}

}  // namespace onix
