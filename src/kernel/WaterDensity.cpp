#include "kernel/WaterDensity.h"

#include <cstdlib>
#include <optional>

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

namespace {

// The parser preserves description keys verbatim — including the UTF-8
// superscript three (³ = 0xC2 0xB3) and degree sign (° = 0xC2 0xB0)
// PHREEQC writes.
const std::string kKeyDensity = "Density (g/cm\xC2\xB3)";
const std::string kKeyVolume  = "Volume (L)";
const std::string kKeyMassW   = "Mass of water (kg)";
const std::string kKeyTempC   = "Temperature (\xC2\xB0""C)";

std::optional<double> parseStrict(const std::map<std::string, std::string>& d,
                                  const std::string& key) {
  auto it = d.find(key);
  if (it == d.end()) return std::nullopt;
  char* end = nullptr;
  const double v = std::strtod(it->second.c_str(), &end);
  if (end == it->second.c_str()) return std::nullopt;
  return v;
}

}

SolutionDensity resolveSolutionDensity(
    const std::map<std::string, std::string>& description,
    double temperature_c) {
  auto v = parseStrict(description, kKeyDensity);
  if (v && *v > 0.5 && *v < 2.0)
    return {*v, DensitySource::Database};
  return {pureWaterDensity(temperature_c), DensitySource::PureWaterFallback};
}

ConvertContextResolution buildConvertContext(
    const std::map<std::string, std::string>& description,
    double fallback_temperature_c) {
  UnitConvertContext ctx;
  ctx.kg_water = parseStrict(description, kKeyMassW).value_or(1.0);
  if (ctx.kg_water <= 0.0) ctx.kg_water = 1.0;

  auto vol = parseStrict(description, kKeyVolume);
  if (vol && *vol > 0.0) {
    ctx.L_solution = *vol;
    return {ctx, VolumeSource::Database};
  }

  const double T = parseStrict(description, kKeyTempC).value_or(
      fallback_temperature_c);
  auto dens = parseStrict(description, kKeyDensity);
  if (dens && *dens > 0.5 && *dens < 2.0) {
    ctx.L_solution = ctx.kg_water / *dens;
    return {ctx, VolumeSource::FromDatabaseDensity};
  }
  const double rho = pureWaterDensity(T);
  if (rho > 0.0) ctx.L_solution = ctx.kg_water / rho;
  return {ctx, VolumeSource::PureWaterFallback};
}

}
