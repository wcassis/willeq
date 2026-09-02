// eq_movement_internal.h -- the movement/following constants shared by the
// three eq_movement*.cpp TUs. Batch-J sanctioned internal header (J23,
// refactor/J_file_size_reclamation.md Process step 4). These are const float
// at namespace scope: internal linkage, one copy per including TU, no ODR
// issue and no link-visible symbol. Do not add to this header "for symmetry"
// -- correction 10 requires each symbol be measured and signed off.
#pragma once

// Movement constants
const float DEFAULT_RUN_SPEED = 6.0f;     // Real EQ run speed based on good client testing
const float DEFAULT_WALK_SPEED = 3.0f;    // Normal EQ walk speed (half of run)
const float WALK_SPEED_THRESHOLD = 4.5f;  // Speed threshold for walk vs run animation
const float POSITION_UPDATE_INTERVAL_MS = 200.0f;  // Real clients update every 200ms

// Following constants
const float FOLLOW_CLOSE_DISTANCE = 10.0f;   // Stop following when this close
const float FOLLOW_FAR_DISTANCE = 30.0f;     // Full speed when this far
const float FOLLOW_MIN_SPEED_MULT = 0.5f;    // Minimum speed multiplier when close
const float FOLLOW_MAX_SPEED_MULT = 1.5f;    // Maximum speed multiplier when far
