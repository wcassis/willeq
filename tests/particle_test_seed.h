// tests/particle_test_seed.h -- B1169 (#488): the particle-RNG seed of every
// test that drives SimulationWorker's particle phase.
//
// SimulationWorker seeds its particle RNG from std::random_device when the
// input's rngSeed is 0 (the client's path, sim_particles.cpp). A test that
// measures a particle cloud against a band then sees a different cloud every
// run: SpellLandingVolume.TheSourceIsABodyWideCloudShapedLikeTheOracles failed
// about 1 run in 300 that way (spell 458's width 2.167 / 2.080 under the 2.2
// floor, CI job 11712). Every such test sets pi.rngSeed to this value, so a
// run is the same run on every host and every repeat;
// SpellParticleSeedWiring (test_spell_landing_volume.cpp) holds every site to
// it. The FX harness's own default (--fx-seed) is the same 1.
#pragma once

#include <cstdint>

namespace particle_test {
constexpr uint32_t kSeed = 1u;
}  // namespace particle_test
