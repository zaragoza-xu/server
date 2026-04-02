#pragma once
#include <format>
#include <iostream>

namespace logging {
template <typename... Args> void log(std::string_view fmt, Args &&...args) {

  std::string msg = std::vformat(fmt, std::make_format_args(args...));
  std::cerr << msg << std::endl;
}
} // namespace logging
