#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cattle_climate {

struct WeatherSample {
    std::string dt_iso;
    int year{};
    int month{};
    int day{};
    int hour{};
    double outdoor_temp_c{};
    double relative_humidity{}; // 0..1
    double wind_speed_m_s{};
    double precipitation_mm{};
    double cloud_cover_fraction{}; // 0..1
    double direct_radiation_w_m2{};
    double diffuse_radiation_w_m2{};
};

class WeatherDataset {
public:
    static WeatherDataset load_csv(const std::string& path);

    [[nodiscard]] bool empty() const noexcept { return samples_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }
    [[nodiscard]] const WeatherSample& at(std::size_t index) const { return samples_.at(index); }
    [[nodiscard]] const std::vector<WeatherSample>& samples() const noexcept { return samples_; }

private:
    std::vector<WeatherSample> samples_{};
};

} // namespace cattle_climate
