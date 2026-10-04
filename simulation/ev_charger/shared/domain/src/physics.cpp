#include "simulation/physics.hpp"
#include <algorithm>
#include <cmath>

namespace simulation {
void Battery::tick(double seconds) {
    power_w = 0;
    if (!charging || faulted || seconds <= 0 || seconds > 60 || soc >= target_soc)
        return;
    const double taper = soc > 80 ? std::clamp((100 - soc) / 20, 0.1, 1.0) : 1.0;
    const double thermal =
        temperature_c > 60 ? std::clamp((85 - temperature_c) / 25, 0.0, 1.0) : 1.0;
    power_w = std::max(0.0, std::min({station_w, vehicle_w * taper, grid_w})) * thermal;
    const double remaining = capacity_wh * (target_soc - soc) / 100 / 0.92;
    const double delivered = std::min(remaining, power_w * seconds / 3600);
    meter_wh += delivered;
    soc = std::min(target_soc, soc + delivered * 0.92 / capacity_wh * 100);
    elapsed_seconds += seconds;
    temperature_c += (25 + power_w / 6000 - temperature_c) * std::min(seconds / 600, 1.0);
}
double estimate_seconds(double capacity_wh, double from_soc, double target_soc, double power_w) {
    if (!std::isfinite(capacity_wh + from_soc + target_soc + power_w) || capacity_wh < 1000 ||
        capacity_wh > 250000 || from_soc < 0 || target_soc > 100 || target_soc <= from_soc ||
        power_w < 100 || power_w > 350000)
        return -1;
    return capacity_wh * (target_soc - from_soc) / 100 / 0.92 / power_w * 3600;
}
} // namespace simulation
