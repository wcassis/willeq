#pragma once

#include <cstdint>

// Entity pose state — owned by the game-state layer (S2 decoupling: moved
// here from IrrlichtRenderer, which previously owned this enum despite game
// state reading it). Values are wire-stable: they are cast to/from uint8_t
// in EntityPoseStateChangedData bridge events.

namespace eqt {
namespace state {

enum class EntityPoseState : uint8_t {
    Standing  = 0,
    Sitting   = 1,
    Crouching = 2,
    Lying     = 3,
};

} // namespace state
} // namespace eqt
