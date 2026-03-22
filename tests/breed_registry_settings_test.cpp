#include "cattle_climate/rl_mpc.hpp"
#include "cattle_climate/thermal_comfort.hpp"

#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const auto project = cattle_climate::load_project_settings_from_json(argv[1]);
    if (project.training_breed_profiles.size() < 3) return 2;
    if (project.training_breed_profiles[0].cow_count != 8) return 3;
    const auto boran = cattle_climate::breed_from_name("boran");
    if (std::abs(boran.area_factor - 1.12) > 1e-9) return 4;
    if (std::abs(boran.rbcsf - 1.30) > 1e-9) return 5;
    const auto registry = cattle_climate::get_breed_registry();
    if (registry.size() < 3) return 6;
    std::cout << "breed_registry_ok\n";
    return 0;
}
