#pragma once

#include "kernel/UnitConvert.h"

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

enum class VolumeSource {
  Database,           // "Volume (L)" reported directly by PHREEQC
  FromDatabaseDensity,// "Density (g/cm³)" → L = kg_water / ρ
  PureWaterFallback,  // Kell ρ(T) → L = kg_water / ρ
};

struct ConvertContextResolution {
  UnitConvertContext context;
  VolumeSource source;
};

// Build a UnitConvertContext for /L unit conversions from a PHREEQC frame's
// description block. Reads:
//   - "Mass of water (kg)"   → ctx.kg_water (defaults to 1.0)
//   - "Volume (L)"           → ctx.L_solution, used directly when present
//   - "Density (g/cm³)" + Mw → L = kg_water / ρ when no Volume key
//   - "Temperature (°C)"     → Kell ρ(T) fallback when neither is given
// `fallback_temperature_c` is used only when the description carries no
// "Temperature (°C)" key.
ConvertContextResolution buildConvertContext(
    const std::map<std::string, std::string>& description,
    double fallback_temperature_c = 25.0);

}
