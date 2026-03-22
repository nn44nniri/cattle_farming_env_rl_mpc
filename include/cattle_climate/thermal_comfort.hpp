#pragma once

#include <string>
#include <vector>

namespace cattle_climate {

struct BreedPreset {
    std::string name{"charolais"};
    double lct_ref_c{5.0};
    double uct_ref_c{20.0};
    double optimal_relative_humidity_min{0.45};
    double optimal_relative_humidity_max{0.75};
    double optimal_wind_min_m_s{0.1};
    double optimal_wind_max_m_s{2.0};
    double heat_production_factor{1.0};
    double coat_reflectivity{0.60};
    double coat_depth_m{0.012};
    double area_factor{1.00};
    double cbsmax{64.1};
    double lasmax_a{3.08};
    double lasmax_b{1.73};
    double rbcsf{1.00};
    double reference_skin_temp_c{35.3};
    double metabolic_multiplier{1.00};
};

struct ComfortInputs {
    double ambient_temp_c{};
    double relative_humidity{}; // 0..1
    double wind_speed_m_s{};
    double precipitation_mm{};
    double cloud_cover_fraction{}; // 0..1
    double solar_radiation_w_m2{};
    double body_weight_kg{};
    double heat_production_factor{1.0};
};

struct TNZRange {
    double lct_c{};
    double uct_c{};
    double tbw_kg{};
    double heat_production_factor{};
    bool below_tnz{false};
    bool within_tnz{false};
    bool above_tnz{false};
};

void set_breed_registry(const std::vector<BreedPreset>& presets);
std::vector<BreedPreset> get_breed_registry();
BreedPreset breed_from_name(const std::string& name);
TNZRange compute_tnz_range(const BreedPreset& breed, const ComfortInputs& inputs);

} // namespace cattle_climate
