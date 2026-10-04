#include "simulation/physics.hpp"
#include <cmath>
#include <iostream>
int main() {
    unsigned failures = 0;
    auto check = [&](bool condition) {
        if (!condition)
            ++failures;
    };
    simulation::Battery battery;
    battery.charging = true;
    for (unsigned i = 0; i < 60; ++i)
        battery.tick(60);
    check(std::abs(battery.meter_wh - 11000) < 1);
    check(battery.soc > 36 && battery.soc < 37);
    battery.grid_w = 1000;
    battery.tick(1);
    check(battery.power_w <= 1000);
    const auto meter = battery.meter_wh;
    battery.faulted = true;
    battery.tick(60);
    check(battery.meter_wh == meter && battery.power_w == 0);
    battery.faulted = false;
    battery.soc = 79.999;
    battery.tick(60);
    check(battery.soc <= 80);
    check(simulation::estimate_seconds(60000, 20, 80, 11000) > 12000);
    check(simulation::estimate_seconds(60000, 80, 20, 11000) == -1);
    if (failures)
        std::cerr << failures << " physics checks failed\n";
    return failures ? 1 : 0;
}
