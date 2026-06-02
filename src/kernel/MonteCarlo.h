#pragma once

#include "kernel/EquilibriumProblem.h"
#include "kernel/PhreeqcOutputParser.h"

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace qtchem {

class DatabaseInfo;
class PhreeqcSession;

// Half-widths of uniform intervals around the baseline. A nullopt or zero
// disables that source. The pH delta is applied only when ph.kind == Fixed.
// concentration_percent[i] is a fractional perturbation (0.05 == ±5%) for
// components[i]; entries beyond components.size() are ignored.
struct UncertaintySpec {
  std::optional<double> temperature_delta_c;
  std::optional<double> pressure_delta_atm;
  std::optional<double> ph_delta;
  std::vector<double> concentration_percent;
  std::optional<std::uint64_t> seed;
};

struct MonteCarloBudget {
  double max_seconds = 2.0;
  int max_runs = 10000;
};

struct ScalarAggregate {
  double baseline = 0.0;
  double min_value = 0.0;
  double max_value = 0.0;
  double mean_l1 = 0.0;  // <|x - baseline|>
  double rms_l2 = 0.0;   // sqrt(<(x - baseline)^2>)
  int n_samples = 0;
};

struct ElementAggregate {
  std::string element;
  ScalarAggregate molality;
};

struct SpeciesAggregate {
  std::string element;
  std::string name;
  ScalarAggregate molality;
  ScalarAggregate activity;
};

struct MonteCarloResult {
  ParsedOutput baseline;
  std::vector<ElementAggregate> elements;
  std::vector<SpeciesAggregate> species;
  int runs_completed = 0;
  double elapsed_seconds = 0.0;
  std::string error;  // non-empty if the baseline solve itself failed
};

// Online aggregator. Construct with the baseline value, feed each
// perturbed sample with add(), then call finalize() to read the stats.
class ScalarAggregator {
 public:
  explicit ScalarAggregator(double baseline) : baseline_(baseline) {}

  void add(double x);
  ScalarAggregate finalize() const;

 private:
  double baseline_;
  int n_ = 0;
  double min_ = 0.0, max_ = 0.0;
  double sum_abs_dev_ = 0.0;
  double sum_sq_dev_ = 0.0;
};

// Produce one perturbed copy of `base` by drawing each enabled source
// uniformly from its ±delta interval. Exposed for tests.
EquilibriumProblem perturbProblem(const EquilibriumProblem& base,
                                   const UncertaintySpec& spec,
                                   std::mt19937& rng);

// Solve the baseline once, then loop perturbed solves until either
// budget.max_seconds elapsed or budget.max_runs reached. Aggregates per
// element / per species and reports how many runs completed.
MonteCarloResult runMonteCarlo(const EquilibriumProblem& baseline,
                                const UncertaintySpec& spec,
                                const MonteCarloBudget& budget,
                                PhreeqcSession& session,
                                const DatabaseInfo* db);

}
