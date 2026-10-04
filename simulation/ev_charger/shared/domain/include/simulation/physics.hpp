#pragma once
#include <cstdint>

namespace simulation {
struct Battery {
    double capacity_wh = 60'000;
    double soc = 20;
    double target_soc = 80;
    double station_w = 22'000;
    double vehicle_w = 11'000;
    double grid_w = 22'000;
    double temperature_c = 25;
    double meter_wh = 0;
    double power_w = 0;
    double elapsed_seconds = 0;
    bool charging = false;
    bool faulted = false;

    void tick(double seconds);
};
double estimate_seconds(double capacity_wh, double from_soc, double target_soc, double power_w);
} // namespace simulation
