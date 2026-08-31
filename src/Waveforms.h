#pragma once

#include <stdint.h>

#include <DgLabControl.h>

namespace waveforms {

struct V2WaveBlock {
  uint8_t bytes[3];
};

const V2WaveBlock& currentV2(char selectedWave, int waveIndex);
const dglab::WaveBlock& currentV3(char selectedWave, int waveIndex);

}
