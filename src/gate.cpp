// lg-gate: decide whether head is slower than base.
//   lg-gate BASE.csv HEAD.csv [--book flat] [--type ALL] [--threshold 0.05]
//           [--confidence 0.95] [--iters 5000] [--summary FILE]
//
// Inputs are lg-bench CSVs, one row per (run, type). For each metric (p99 and p99.9) the gate
// takes one value per run from each side and bootstraps a confidence interval for
// median(head) / median(base). It fails (exit 1) only when the interval's lower bound is above
// 1 + threshold, i.e. when the slowdown is larger than both the threshold and the noise.
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "lg/gate.hpp"
#include "lg/stats.hpp"
#include "lg/util.hpp"

namespace {

struct Table {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;

  int col(const std::string& name) const {
    for (std::size_t i = 0; i < header.size(); ++i)
      if (header[i] == name) return int(i);
    return -1;
  }
};

std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> out;
  std::stringstream ss(line);
  std::string cell;
  while (std::getline(ss, cell, ',')) out.push_back(cell);
  return out;
}

Table load(const std::string& path) {
  Table t;
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::string header_line, line;
  if (std::getline(f, header_line)) t.header = split(header_line);
  while (std::getline(f, line))
    if (!line.empty() && line != header_line) t.rows.push_back(split(line));
  return t;
}

// One value per run for the given metric, filtered by book and message type.
std::vector<double> per_run(const Table& t, const std::string& book, const std::string& type,
                            const std::string& metric) {
  const int cb = t.col("book"), ct = t.col("type"), cm = t.col(metric), cr = t.col("run");
  if (cb < 0 || ct < 0 || cm < 0 || cr < 0) throw std::runtime_error("unexpected CSV header");
  std::map<std::string, double> by_run;  // a repeated run id keeps the last value
  for (const auto& r : t.rows)
    if (int(r.size()) > cm && r[std::size_t(cb)] == book && r[std::size_t(ct)] == type)
      by_run[r[std::size_t(cr)]] = std::stod(r[std::size_t(cm)]);
  std::vector<double> v;
  for (auto& [_, x] : by_run) v.push_back(x);
  return v;
}

}  // namespace

int main(int argc, char** argv) {
  lg::Args args(argc, argv);
  const std::string base_path = args.positional(0), head_path = args.positional(1);
  if (base_path.empty() || head_path.empty()) {
    std::fprintf(stderr,
                 "usage: lg-gate BASE.csv HEAD.csv [--book flat] [--type ALL] "
                 "[--threshold 0.05] [--confidence 0.95] [--iters 5000] [--summary FILE]\n");
    return 2;
  }
  const std::string book = args.get("--book", "flat");
  const std::string type = args.get("--type", "ALL");
  const double threshold = args.real("--threshold", 0.05);
  const double confidence = args.real("--confidence", 0.95);
  const int iters = int(args.num("--iters", 5000));

  const Table base = load(base_path), head = load(head_path);

  std::ostringstream md;
  md << "### Latency gate (" << book << " book, " << type << " messages)\n\n"
     << "Fails only if the " << int(confidence * 100) << "% bootstrap interval for "
     << "median(head) / median(base) lies entirely above " << 1.0 + threshold << ".\n\n"
     << "| metric | runs (base/head) | base median ns | head median ns | ratio | "
     << int(confidence * 100) << "% CI | verdict |\n|---|---|---|---|---|---|---|\n";

  bool failed = false;
  for (const char* metric : {"p99_ns", "p999_ns"}) {
    const auto b = per_run(base, book, type, metric);
    const auto h = per_run(head, book, type, metric);
    const lg::GateResult g = lg::judge(b, h, threshold, iters, confidence);
    if (g.verdict == lg::Verdict::Regression) failed = true;
    char line[512];
    std::snprintf(line, sizeof line, "| %s | %zu/%zu | %.0f | %.0f | %.3f | [%.3f, %.3f] | %s |\n",
                  metric, b.size(), h.size(), lg::median(b), lg::median(h), g.ratio.point,
                  g.ratio.lo, g.ratio.hi, std::string(lg::to_string(g.verdict)).c_str());
    md << line;
  }
  md << "\n" << (failed ? "**Result: FAIL** (tail latency regressed)" : "**Result: PASS**")
     << "\n";

  std::fputs(md.str().c_str(), stdout);
  const std::string summary = args.get("--summary");
  if (!summary.empty()) {
    std::ofstream s(summary, std::ios::app);
    s << md.str();
  }
  return failed ? 1 : 0;
}
