#include "kernel/WaterDensity.h"

#include <cstdlib>

namespace qtchem {

double pureWaterDensity(double T) {
  if (T < -50.0 || T > 250.0) return 0.0;
  // Kell 1975 polynomial; coefficients yield kg/m³ — divide by 1000.
  static constexpr double a0 = 999.83952;
  static constexpr double a1 = 16.945176;
  static constexpr double a2 = -7.9870401e-3;
  static constexpr double a3 = -46.170461e-6;
  static constexpr double a4 = 105.56302e-9;
  static constexpr double a5 = -280.54253e-12;
  static constexpr double b  = 16.879850e-3;
  const double num = a0 + T*(a1 + T*(a2 + T*(a3 + T*(a4 + T*a5))));
  const double den = 1.0 + b*T;
  return (num / den) / 1000.0;
}

SolutionDensity resolveSolutionDensity(
    const std::map<std::string, std::string>& description,
    double temperature_c) {
  // The parser preserves the key verbatim — including the UTF-8 superscript
  // three (³ = 0xC2 0xB3) PHREEQC writes.
  static const std::string kKey = "Density (g/cm\xC2\xB3)";
  auto it = description.find(kKey);
  if (it != description.end()) {
    char* end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    if (end != it->second.c_str() && v > 0.5 && v < 2.0)
      return {v, DensitySource::Database};
  }
  return {pureWaterDensity(temperature_c), DensitySource::PureWaterFallback};
}

}
