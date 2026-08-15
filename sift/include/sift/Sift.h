#ifndef SIFT_SIFT_H
#define SIFT_SIFT_H

#include <string>

namespace sift {

inline constexpr int format_version = 1;

enum class Format { Html, Css, JavaScript, Jsx, Json, Xml, Svg };

bool html(const std::string& input, std::string& output, std::string& error);
bool css(const std::string& input, std::string& output, std::string& error);
bool javascript(const std::string& input, std::string& output, std::string& error);
bool jsx(const std::string& input, std::string& output, std::string& error);
bool json(const std::string& input, std::string& output, std::string& error);
bool xml(const std::string& input, std::string& output, std::string& error);
bool svg(const std::string& input, std::string& output, std::string& error);

bool format_for_extension(const std::string& extension, Format& format);
bool run(Format format, const std::string& input, std::string& output, std::string& error);

} // namespace sift

#endif
