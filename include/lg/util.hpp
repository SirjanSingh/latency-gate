// Small helpers shared by the command-line tools.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lg {

inline std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Minimal "--name value" / "--flag" parser; the first argument without "--" is the positional one.
class Args {
 public:
  Args(int argc, char** argv) : args_(argv + 1, argv + argc) {}

  std::string get(std::string_view name, std::string def = "") const {
    for (std::size_t i = 0; i + 1 < args_.size(); ++i)
      if (args_[i] == name) return args_[i + 1];
    return def;
  }

  uint64_t num(std::string_view name, uint64_t def) const {
    const std::string v = get(name);
    return v.empty() ? def : std::strtoull(v.c_str(), nullptr, 10);
  }

  double real(std::string_view name, double def) const {
    const std::string v = get(name);
    return v.empty() ? def : std::strtod(v.c_str(), nullptr);
  }

  bool flag(std::string_view name) const {
    for (const auto& a : args_)
      if (a == name) return true;
    return false;
  }

  std::string positional(std::size_t index = 0) const {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < args_.size(); ++i) {
      if (args_[i].rfind("--", 0) == 0) {
        // skip the value of options that take one
        if (i + 1 < args_.size() && args_[i + 1].rfind("--", 0) != 0 && !is_bare_flag(args_[i])) ++i;
        continue;
      }
      if (seen++ == index) return args_[i];
    }
    return "";
  }

 private:
  static bool is_bare_flag(const std::string& a) { return a == "--check" || a == "--help"; }
  std::vector<std::string> args_;
};

}  // namespace lg
