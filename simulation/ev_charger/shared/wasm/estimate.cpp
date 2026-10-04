#include "simulation/physics.hpp"
#include <emscripten/emscripten.h>
extern "C" EMSCRIPTEN_KEEPALIVE double sim_estimate_seconds(double capacity_wh, double from_soc,
                                                            double target_soc, double power_w) {
    return simulation::estimate_seconds(capacity_wh, from_soc, target_soc, power_w);
}
