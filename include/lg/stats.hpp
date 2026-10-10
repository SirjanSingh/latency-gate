// Percentiles and the bootstrap used by the gate.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace lg {

// Nearest-rank percentile of an already sorted vector. q in (0, 1].
template <class T>
inline T percentile_sorted(const std::vector<T>& v, double q) {
  if (v.empty()) return T{};
  std::size_t rank = std::size_t(std::ceil(q * double(v.size())));
  if (rank == 0) rank = 1;
  if (rank > v.size()) rank = v.size();
  return v[rank - 1];
}

inline double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct Interval {
  double point = 0;  // statistic on the observed data
  double lo = 0;     // lower confidence bound
  double hi = 0;     // upper confidence bound
};

// Bootstrap confidence interval for median(head) / median(base).
// Each input is one value per benchmark run (for example that run's p99), so runs are the
// resampling unit: a single noisy run cannot dominate the verdict.
inline Interval bootstrap_ratio_of_medians(const std::vector<double>& base,
                                           const std::vector<double>& head, int iters = 5000,
                                           double confidence = 0.95, uint64_t seed = 1) {
  Interval out;
  if (base.empty() || head.empty()) return out;
  const double mb = median(base);
  out.point = mb > 0 ? median(head) / mb : 0.0;

  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::size_t> pick_b(0, base.size() - 1);
  std::uniform_int_distribution<std::size_t> pick_h(0, head.size() - 1);
  std::vector<double> rb(base.size()), rh(head.size()), ratios;
  ratios.reserve(std::size_t(iters));
  for (int it = 0; it < iters; ++it) {
    for (auto& x : rb) x = base[pick_b(rng)];
    for (auto& x : rh) x = head[pick_h(rng)];
    const double b = median(rb);
    if (b > 0) ratios.push_back(median(rh) / b);
  }
  std::sort(ratios.begin(), ratios.end());
  const double alpha = (1.0 - confidence) / 2.0;
  out.lo = percentile_sorted(ratios, alpha);
  out.hi = percentile_sorted(ratios, 1.0 - alpha);
  return out;
}

}  // namespace lg
