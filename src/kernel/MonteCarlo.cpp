#include "kernel/MonteCarlo.h"

#include "kernel/PhreeqcSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace qtchem {

namespace {

using Clock = std::chrono::steady_clock;

double drawUniform(std::mt19937& rng, double half_width) {
  std::uniform_real_distribution<double> u(-1.0, 1.0);
  return u(rng) * half_width;
}

}  // namespace

void ScalarAggregator::add(double x) {
  if (n_ == 0) {
    min_ = max_ = x;
  } else {
    if (x < min_) min_ = x;
    if (x > max_) max_ = x;
  }
  const double dev = x - baseline_;
  sum_abs_dev_ += std::abs(dev);
  sum_sq_dev_  += dev * dev;
  ++n_;
}

ScalarAggregate ScalarAggregator::finalize() const {
  ScalarAggregate out;
  out.baseline = baseline_;
  out.n_samples = n_;
  if (n_ == 0) {
    out.min_value = out.max_value = baseline_;
    return out;
  }
  out.min_value = min_;
  out.max_value = max_;
  out.mean_l1 = sum_abs_dev_ / n_;
  out.rms_l2  = std::sqrt(sum_sq_dev_ / n_);
  return out;
}

EquilibriumProblem perturbProblem(const EquilibriumProblem& base,
                                  const UncertaintySpec& spec,
                                  std::mt19937& rng) {
  EquilibriumProblem p = base;
  if (spec.temperature_delta_c && *spec.temperature_delta_c > 0.0) {
    p.temperature_c += drawUniform(rng, *spec.temperature_delta_c);
  }
  if (spec.pressure_delta_atm && *spec.pressure_delta_atm > 0.0) {
    p.pressure_atm += drawUniform(rng, *spec.pressure_delta_atm);
    if (p.pressure_atm <= 0.0) p.pressure_atm = 1e-3;
  }
  if (spec.ph_delta && *spec.ph_delta > 0.0 && p.ph.kind == PhSpec::Fixed) {
    p.ph.value += drawUniform(rng, *spec.ph_delta);
  }
  const size_t n = std::min(p.components.size(),
                            spec.concentration_percent.size());
  for (size_t i = 0; i < n; ++i) {
    const double pct = spec.concentration_percent[i];
    if (pct > 0.0) {
      p.components[i].total *= (1.0 + drawUniform(rng, pct));
      if (p.components[i].total < 0.0) p.components[i].total = 0.0;
    }
  }
  return p;
}

MonteCarloResult runMonteCarlo(const EquilibriumProblem& baseline_problem,
                               const UncertaintySpec& spec,
                               const MonteCarloBudget& budget,
                               PhreeqcSession& session,
                               const DatabaseInfo* db) {
  MonteCarloResult res;
  const auto t0 = Clock::now();

  // Baseline solve.
  auto base = session.solveEquilibrium(baseline_problem, db);
  if (!base.ok) {
    res.error = base.error_string.empty()
                    ? "baseline solve failed" : base.error_string;
    return res;
  }
  res.baseline = parsePhreeqcOutput(base.raw_output);
  if (res.baseline.frames.empty()) {
    res.error = "baseline output had no frames";
    return res;
  }
  std::vector<std::string> element_names;
  element_names.reserve(baseline_problem.components.size());
  for (const auto& c : baseline_problem.components)
    element_names.push_back(c.element);
  session.refineParsedTotals(res.baseline, element_names);
  const auto& base_final = res.baseline.frames.back();

  // Build per-key aggregators keyed by element root and (element, species).
  std::map<std::string, ScalarAggregator> elem_agg;
  std::map<std::pair<std::string, std::string>,
           std::pair<ScalarAggregator, ScalarAggregator>> sp_agg;
  std::vector<std::string> elem_order;
  std::vector<std::pair<std::string, std::string>> sp_order;
  for (const auto& t : base_final.totals) {
    const std::string root = elementRoot(t.element);
    if (elem_agg.emplace(root, ScalarAggregator(t.molality)).second)
      elem_order.push_back(root);
  }
  for (const auto& s : base_final.species) {
    auto key = std::make_pair(s.element, s.name);
    if (sp_agg.emplace(key,
                       std::make_pair(ScalarAggregator(s.molality),
                                      ScalarAggregator(s.activity))).second)
      sp_order.push_back(key);
  }

  // Perturbed loop.
  const std::uint64_t seed = spec.seed.value_or(std::random_device{}());
  std::mt19937 rng(seed);
  const auto budget_end = t0 + std::chrono::duration<double>(budget.max_seconds);
  while (res.runs_completed < budget.max_runs &&
         Clock::now() < budget_end) {
    auto perturbed = perturbProblem(baseline_problem, spec, rng);
    auto r = session.solveEquilibrium(perturbed, db);
    if (!r.ok) continue;
    auto po = parsePhreeqcOutput(r.raw_output);
    if (po.frames.empty()) continue;
    session.refineParsedTotals(po, element_names);
    const auto& f = po.frames.back();
    for (const auto& t : f.totals) {
      auto it = elem_agg.find(elementRoot(t.element));
      if (it != elem_agg.end()) it->second.add(t.molality);
    }
    for (const auto& s : f.species) {
      auto it = sp_agg.find({s.element, s.name});
      if (it != sp_agg.end()) {
        it->second.first.add(s.molality);
        it->second.second.add(s.activity);
      }
    }
    ++res.runs_completed;
  }

  for (const auto& root : elem_order) {
    ElementAggregate ea;
    ea.element = root;
    ea.molality = elem_agg.at(root).finalize();
    res.elements.push_back(ea);
  }
  for (const auto& key : sp_order) {
    SpeciesAggregate sa;
    sa.element = key.first;
    sa.name = key.second;
    sa.molality = sp_agg.at(key).first.finalize();
    sa.activity = sp_agg.at(key).second.finalize();
    res.species.push_back(sa);
  }

  res.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - t0).count();
  return res;
}

}
