#include "cattle_climate/thermal_comfort.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace cattle_climate {
namespace {

std::string normalize(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ch == ' ' || ch == '-') ch = '_';
    }
    return text;
}

std::vector<BreedPreset> default_breed_registry() {
    return {
        BreedPreset{"charolais", 5.0, 20.0, 0.45, 0.75, 0.10, 2.20, 1.00, 0.60, 0.012, 1.00, 64.1, 3.08, 1.73, 1.00, 35.3, 1.00},
        BreedPreset{"boran", 7.0, 24.0, 0.40, 0.72, 0.10, 2.50, 1.10, 0.60, 0.012, 1.12, 64.1, 4.89, 0.80, 1.30, 34.5, 0.91},
        BreedPreset{"brahman_shorthorn", 6.0, 26.0, 0.40, 0.72, 0.10, 2.70, 1.08, 0.56, 0.012, 1.09, 64.1, 4.44, 1.03, 1.00, 34.7, 0.93}
    };
}

std::vector<BreedPreset>& registry_storage() {
    static std::vector<BreedPreset> registry = default_breed_registry();
    return registry;
}

} // namespace

void set_breed_registry(const std::vector<BreedPreset>& presets) {
    registry_storage() = presets.empty() ? default_breed_registry() : presets;
}

std::vector<BreedPreset> get_breed_registry() {
    return registry_storage();
}

BreedPreset breed_from_name(const std::string& name) {
    const std::string key = normalize(name);
    for (const auto& preset : registry_storage()) {
        if (normalize(preset.name) == key) return preset;
    }
    if (key == "brahman(3/4)_x_shorthorn(1/4)" || key == "brahman") return breed_from_name("brahman_shorthorn");
    throw std::runtime_error("Unsupported cattle breed preset: " + name);
}

TNZRange compute_tnz_range(const BreedPreset& breed, const ComfortInputs& inputs) {
    TNZRange out{};
    out.tbw_kg = inputs.body_weight_kg;
    out.heat_production_factor = inputs.heat_production_factor;

    const double weight_shift = (450.0 - inputs.body_weight_kg) / 100.0;
    const double wind_shift = 0.45 * std::max(0.0, inputs.wind_speed_m_s - breed.optimal_wind_max_m_s);
    const double rain_shift = 0.20 * std::min(inputs.precipitation_mm, 10.0);
    const double humidity_shift = 20.0 * std::max(0.0, inputs.relative_humidity - breed.optimal_relative_humidity_max);
    const double solar_shift = (0.004 + 0.002 * (1.0 - breed.coat_reflectivity)) * std::max(0.0, inputs.solar_radiation_w_m2 - 250.0);
    const double cloud_shift = 0.8 * inputs.cloud_cover_fraction * (1.0 + 0.05 * breed.coat_depth_m * 1000.0);
    const double metabolic_shift = 1.5 * ((inputs.heat_production_factor * breed.metabolic_multiplier) - 1.0);
    const double area_shift = 0.8 * (breed.area_factor - 1.0);
    const double coat_shift = 12.0 * (breed.coat_depth_m - 0.012);
    const double tissue_shift = -0.015 * (breed.cbsmax - 64.1);
    const double sweating_shift = -0.12 * (breed.lasmax_a - 3.08) - 0.18 * (breed.lasmax_b - 1.73);
    const double skin_temp_shift = 0.12 * (35.3 - breed.reference_skin_temp_c);
    const double blood_shift = -0.60 * (breed.rbcsf - 1.0);

    out.lct_c = breed.lct_ref_c + 0.8 * weight_shift + wind_shift + rain_shift - solar_shift + 0.4 * cloud_shift
              - 0.8 * metabolic_shift - 0.3 * area_shift + coat_shift + tissue_shift + skin_temp_shift + 0.3 * blood_shift;
    out.uct_c = breed.uct_ref_c + 0.5 * weight_shift - 0.35 * wind_shift - 0.2 * rain_shift - 0.3 * cloud_shift
              - 0.9 * metabolic_shift - humidity_shift + 0.003 * std::max(0.0, inputs.solar_radiation_w_m2 - 200.0)
              + 0.7 * area_shift - 0.4 * coat_shift - sweating_shift - skin_temp_shift - blood_shift;

    if (out.uct_c < out.lct_c + 3.0) out.uct_c = out.lct_c + 3.0;
    out.below_tnz = inputs.ambient_temp_c < out.lct_c;
    out.above_tnz = inputs.ambient_temp_c > out.uct_c;
    out.within_tnz = !out.below_tnz && !out.above_tnz;
    return out;
}

} // namespace cattle_climate
