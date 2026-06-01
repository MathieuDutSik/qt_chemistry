#pragma once

#include <map>
#include <string>

namespace qtchem {

// Density of pure liquid water at 1 atm, in kg/L (== g/cm³).
// Kell, J. Chem. Eng. Data 20 (1975) 97. Valid 0–150 °C; pressure
// correction is sub-1e-4 over the 0–10 atm range we care about, so it
// is not applied. Returns 0 for nonsensical temperatures.
double pureWaterDensity(double temperature_c);

enum class DensitySource {
  Database,           // taken from PHREEQC's "Description of solution"
  PureWaterFallback,  // computed via pureWaterDensity()
};

struct SolutionDensity {
  double kg_per_L;
  DensitySource source;
};

// Pick the best solution density for /L unit conversions. Prefers the
// "Density (g/cm³)" entry from PHREEQC's description block when present and
// physically sensible; otherwise falls back to pureWaterDensity at the run
// temperature.
SolutionDensity resolveSolutionDensity(
    const std::map<std::string, std::string>& description,
    double temperature_c);

}
