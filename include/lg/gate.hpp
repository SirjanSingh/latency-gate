// The regression decision. A change only fails when the whole confidence interval for
// head/base sits above 1 + threshold, so ordinary run-to-run noise does not raise an alarm.
#pragma once

#include <string_view>
#include <vector>

#include "lg/stats.hpp"

namespace lg {

enum class Verdict { NoChange, Regression, Improvement, InsufficientData };

inline std::string_view to_string(Verdict v) {
  switch (v) {
    case Verdict::NoChange: return "no change";
    case Verdict::Regression: return "REGRESSION";
    case Verdict::Improvement: return "improvement";
    case Verdict::InsufficientData: return "insufficient data";
  }
  return "?";
}

struct GateResult {
  Verdict verdict = Verdict::InsufficientData;
  Interval ratio;
};

inline GateResult judge(const std::vector<double>& base, const std::vector<double>& head,
                        double threshold, int iters = 5000, double confidence = 0.95,
                        uint64_t seed = 1) {
  GateResult r;
  if (base.size() < 3 || head.size() < 3) return r;
  r.ratio = bootstrap_ratio_of_medians(base, head, iters, confidence, seed);
  if (r.ratio.lo > 1.0 + threshold) r.verdict = Verdict::Regression;
  else if (r.ratio.hi < 1.0 - threshold) r.verdict = Verdict::Improvement;
  else r.verdict = Verdict::NoChange;
  return r;
}

}  // namespace lg
