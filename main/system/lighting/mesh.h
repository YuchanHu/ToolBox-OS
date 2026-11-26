#pragma once
#include "radio.h"
namespace toolbox::lighting::mesh {
void start(Brand brand);
void stop();
void tick();
RadioState status();
void connect(const Device &device);
void send(const State &state, bool force = false);
} // namespace toolbox::lighting::mesh
