# State Management System Updates

## Overview

This document outlines the updates required to bring the state management system in sync with features added to main since the branch was created. The original refactor (Phases 1-6) created a foundation with GameState containing PlayerState, EntityManager, WorldState, CombatState, GroupState, and DoorState. Several new features have been added to main that need to be incorporated.

## New Features in Main (Since Branch Creation)

| Feature | Commits | Key Files Added |
|---------|---------|-----------------|
| Pet System | eb97058 | `pet_constants.h`, `pet_window.h` |
| Skill Trainer | 929e851 | `skill_trainer_window.h` |
| Bank Window | 0a0520e | `bank_window.h` |
| Tradeskill Containers | 11b358a | `tradeskill_container_window.h` |
| Zone Lines | 691e1b6, 123d5fb | `zone_lines.h`, `data/zone_lines.json` |
| Animation System | f973d97, 043a335, 99fda91 | `animation_constants.h`, `skeletal_animator.h` |
| Jump Animation | c04a483 | Updates to player state |
| Hotbar Skills | 3224f33 | Updates to skill system |
| Performance Opts | 80ddfb2 | Various rendering improvements |

---

## Required Updates

### 1. NEW: PetState Class

**Priority:** High
**Reason:** Pet system is a major feature in main with UI window and commands.

**Location:** `include/client/state/pet_state.h`, `src/client/state/pet_state.cpp`

**State to track:**
```cpp
class PetState {
    uint16_t m_petSpawnId = 0;        // 0 = no pet
    std::string m_petName;
    uint8_t m_petLevel = 0;
    uint8_t m_petHPPercent = 100;
    uint8_t m_petManaPercent = 100;

    // Button toggle states (10 states from PetButton enum)
    std::array<bool, 10> m_buttonStates = {};  // sit, stop, regroup, follow, guard, taunt, hold, ghold, focus, spellhold

public:
    // Queries
    bool hasPet() const;
    uint16_t petSpawnId() const;
    std::string petName() const;
    uint8_t petLevel() const;
    uint8_t petHPPercent() const;
    uint8_t petManaPercent() const;
    bool getButtonState(uint8_t button) const;

    // Mutations (fire events)
    void setPet(uint16_t spawnId, const std::string& name, uint8_t level);
    void clearPet();
    void updatePetHP(uint8_t hpPercent);
    void updatePetMana(uint8_t manaPercent);
    void setButtonState(uint8_t button, bool state);

    // Reset
    void reset();
};
```

**Events to add to EventBus:**
- `PetCreated` - Pet spawned
- `PetRemoved` - Pet dismissed/died
- `PetStatsChanged` - HP/mana updated
- `PetButtonStateChanged` - Toggle state changed

**EQ class members to mirror:**
- `m_pet_spawn_id` (line 910)
- `m_pet_button_states[10]` (line 911)

---

### 2. UPDATE: PlayerState - Add Bank/Trainer/Vendor State

**Priority:** High
**Reason:** Bank, trainer, and vendor interactions are tracked in EQ class.

**Add to PlayerState:**
```cpp
// Bank currency (separate from carried currency)
uint32_t m_bankPlatinum = 0;
uint32_t m_bankGold = 0;
uint32_t m_bankSilver = 0;
uint32_t m_bankCopper = 0;

// Practice/training points
uint32_t m_practicePoints = 0;

// NPC interaction state (0 = not interacting)
uint16_t m_vendorNpcId = 0;
float m_vendorSellRate = 1.0f;
std::string m_vendorName;

uint16_t m_bankerNpcId = 0;

uint16_t m_trainerNpcId = 0;
std::string m_trainerName;

// Loot state
uint16_t m_lootingCorpseId = 0;
```

**Methods to add:**
```cpp
// Bank currency
uint32_t bankPlatinum() const;
uint32_t bankGold() const;
uint32_t bankSilver() const;
uint32_t bankCopper() const;
void setBankCurrency(uint32_t plat, uint32_t gold, uint32_t silver, uint32_t copper);
uint64_t totalBankCopperValue() const;

// Practice points
uint32_t practicePoints() const;
void setPracticePoints(uint32_t points);
void decrementPracticePoints();

// Vendor
bool isWithVendor() const;
uint16_t vendorNpcId() const;
float vendorSellRate() const;
const std::string& vendorName() const;
void setVendor(uint16_t npcId, float sellRate, const std::string& name);
void clearVendor();

// Banker
bool isAtBank() const;
uint16_t bankerNpcId() const;
void setBanker(uint16_t npcId);
void clearBanker();

// Trainer
bool isWithTrainer() const;
uint16_t trainerNpcId() const;
const std::string& trainerName() const;
void setTrainer(uint16_t npcId, const std::string& name);
void clearTrainer();

// Looting
bool isLooting() const;
uint16_t lootingCorpseId() const;
void setLootingCorpse(uint16_t corpseId);
void clearLootingCorpse();
```

**Events to add:**
- `VendorWindowOpened`, `VendorWindowClosed`
- `BankWindowOpened`, `BankWindowClosed`
- `TrainerWindowOpened`, `TrainerWindowClosed`

**EQ class members to mirror:**
- `m_bank_platinum`, `m_bank_gold`, `m_bank_silver`, `m_bank_copper` (lines 951-954)
- `m_practice_points` (line 957)
- `m_vendor_npc_id`, `m_vendor_sell_rate`, `m_vendor_name` (lines 1173-1175)
- `m_banker_npc_id` (line 1178)
- `m_trainer_npc_id`, `m_trainer_name` (lines 1181-1182)
- `m_player_looting_corpse_id` (line 1166)

---

### 3. NEW: TradeskillState Class

**Priority:** Medium
**Reason:** Tradeskill containers (world objects and inventory bags) need tracking.

**Location:** `include/client/state/tradeskill_state.h`, `src/client/state/tradeskill_state.cpp`

**State to track:**
```cpp
class TradeskillState {
    // Active world object container (forge, loom, etc.)
    uint32_t m_activeObjectId = 0;
    std::string m_containerName;
    uint8_t m_containerType = 0;
    uint8_t m_slotCount = 0;

    // Alternative: inventory container (bag)
    int16_t m_activeInventorySlot = -1;

public:
    bool isContainerOpen() const;
    bool isWorldContainer() const;
    bool isInventoryContainer() const;

    // World object container
    uint32_t activeObjectId() const;
    void openWorldContainer(uint32_t dropId, const std::string& name, uint8_t type, uint8_t slots);

    // Inventory container
    int16_t activeInventorySlot() const;
    void openInventoryContainer(int16_t slot, const std::string& name, uint8_t type, uint8_t slots);

    void closeContainer();
    void reset();
};
```

**Events:**
- `TradeskillContainerOpened`
- `TradeskillContainerClosed`

**EQ class members to mirror:**
- `m_active_tradeskill_object_id` (line 919)

---

### 4. UPDATE: EntityManager Entity Struct

**Priority:** Medium
**Reason:** Entity struct in EntityManager needs pet and weapon fields.

**Add to Entity struct in entity_manager.h:**
```cpp
// Pet tracking (from eq.h Entity struct lines 428-430)
uint8_t is_pet = 0;           // Non-zero if this entity is a pet
uint32_t pet_owner_id = 0;    // Spawn ID of pet's owner

// Weapon skill types for combat animations (from eq.h lines 420-421)
uint8_t primary_weapon_skill = 255;    // 255 = unknown/none
uint8_t secondary_weapon_skill = 255;
```

**Methods to add:**
```cpp
// Pet queries
bool isPet(uint16_t spawnId) const;
uint32_t getPetOwnerId(uint16_t spawnId) const;
uint16_t findPetByOwner(uint16_t ownerSpawnId) const;
std::vector<uint16_t> getAllPets() const;
```

---

### 5. UPDATE: WorldState - Zone Line State

**Priority:** Medium
**Reason:** Zone line system added with detailed tracking.

**Current state (already exists):**
- `m_zoneLineTriggered`
- `m_zoneLineTriggerTime`
- `m_lastZoneCheckX/Y/Z`
- `m_pendingZoneId/X/Y/Z/Heading`
- `m_zoneChangeRequested`
- `m_zoneChangeApproved`

**Additional state to add:**
```cpp
// Zoning feature toggle
bool m_zoningEnabled = true;  // Whether zone line detection triggers zoning
```

**Methods to add:**
```cpp
bool isZoningEnabled() const;
void setZoningEnabled(bool enabled);
```

**EQ class member to mirror:**
- `m_zoning_enabled` (line 1021)

---

### 6. UPDATE: ActionDispatcher - New Actions

**Priority:** High
**Reason:** New features need action support.

**Pet Actions:**
```cpp
ActionResult sendPetCommand(uint8_t command, uint16_t targetId = 0);
ActionResult dismissPet();
```

**Tradeskill Actions:**
```cpp
ActionResult clickObject(uint32_t dropId);
ActionResult tradeskillCombine(int16_t containerSlot);
ActionResult closeContainer(uint32_t dropId);
```

**Training Actions:**
```cpp
ActionResult trainSkill(uint8_t skillId);
```

**Bank Actions:**
```cpp
ActionResult convertCurrency(uint8_t fromType, bool convertAll = false);
```

---

### 7. UPDATE: InputHandler - New Input Actions

**Priority:** Medium
**Reason:** New keybindings in main.

**Add to InputAction enum:**
```cpp
TogglePetWindow,    // P key in Player Mode
PetAttack,          // Pet attack command
PetBack,            // Pet back off command
PetFollow,          // Pet follow command
PetGuard,           // Pet guard command
PetSit,             // Pet sit command
```

---

### 8. UPDATE: CommandProcessor - New Commands

**Priority:** Medium
**Reason:** New slash commands added.

**Pet Commands:**
```cpp
/pet <command>  // attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss
```

**Skill Commands (already may exist, verify):**
```cpp
/skills         // Toggle skills window
```

---

### 9. UPDATE: EventBus - New Event Types

**Priority:** High
**Reason:** Events needed for UI updates.

**Add to GameEventType enum:**
```cpp
// Pet events
PetCreated,
PetRemoved,
PetStatsChanged,
PetButtonStateChanged,

// Window events
VendorWindowOpened,
VendorWindowClosed,
BankWindowOpened,
BankWindowClosed,
TrainerWindowOpened,
TrainerWindowClosed,
TradeskillContainerOpened,
TradeskillContainerClosed,

// Zone events
ZoneLineTriggered,
```

**Add event data structs:**
```cpp
struct PetCreatedData {
    uint16_t spawnId;
    std::string name;
    uint8_t level;
};

struct PetButtonStateChangedData {
    uint8_t button;
    bool state;
};

struct WindowOpenedData {
    uint16_t npcId;
    std::string npcName;
};
```

---

### 10. FUTURE: InventoryState Class

**Priority:** Low (deferred)
**Reason:** Complex system, would require significant work.

The original plan mentioned InventoryState but it wasn't implemented. The bank window and tradeskill containers highlight the need for proper inventory state tracking. This is a larger undertaking that should be planned separately.

**Would include:**
- Inventory slots (equipment + general)
- Bank slots (16 + 2 shared)
- Tradeskill container slots (world objects)
- Item instance data
- Cursor item

---

### 11. FUTURE: SpellState Class

**Priority:** Low (deferred)
**Reason:** SpellManager exists, but for clean separation would need SpellState.

**Would include:**
- Memorized spells (8 gem slots)
- Pending scribe state
- Spellbook contents
- Spell cooldowns

---

## Implementation Order

### Phase 1: Core State Updates (Required for feature parity) - COMPLETED

**Status:** ✅ COMPLETE (2026-01-10)

**What was implemented:**

1. **PetState class** (`include/client/state/pet_state.h`, `src/client/state/pet_state.cpp`)
   - Pet identity: spawnId, name, level
   - Pet stats: HP percent, mana percent
   - Button toggle states (10 buttons: sit, stop, regroup, follow, guard, taunt, hold, ghold, focus, spellhold)
   - Events: PetCreated, PetRemoved, PetStatsChanged, PetButtonStateChanged
   - Methods: setPet(), clearPet(), updatePetHP/Mana/Stats(), getButtonState(), setButtonState(), reset()

2. **PlayerState updates** (`include/client/state/player_state.h`, `src/client/state/player_state.cpp`)
   - Bank currency: bankPlatinum/Gold/Silver/Copper, setBankCurrency(), totalBankCopperValue()
   - Practice points: practicePoints(), setPracticePoints(), decrementPracticePoints()
   - Vendor interaction: vendorNpcId, vendorSellRate, vendorName, setVendor(), clearVendor()
   - Bank interaction: bankerNpcId, setBanker(), clearBanker()
   - Trainer interaction: trainerNpcId, trainerName, setTrainer(), clearTrainer()
   - Loot state: lootingCorpseId, setLootingCorpse(), clearLootingCorpse()
   - Window events fired on setVendor/setBanker/setTrainer and their clear methods

3. **EntityManager Entity struct updates** (`include/client/state/entity_manager.h`, `src/client/state/entity_manager.cpp`)
   - Pet tracking fields: isPet, petOwnerId
   - Weapon skill types: primaryWeaponSkill, secondaryWeaponSkill
   - Pet query methods: isPet(), getPetOwnerId(), findPetByOwner(), getAllPets()

4. **EventBus updates** (`include/client/state/event_bus.h`)
   - Pet events: PetCreated, PetRemoved, PetStatsChanged, PetButtonStateChanged
   - Window events: VendorWindowOpened/Closed, BankWindowOpened/Closed, TrainerWindowOpened/Closed, TradeskillContainerOpened/Closed
   - Event data structs: PetCreatedData, PetRemovedData, PetStatsChangedData, PetButtonStateChangedData, WindowOpenedData, WindowClosedData
   - All new types added to EventData variant

5. **GameState updates** (`include/client/state/game_state.h`, `src/client/state/game_state.cpp`)
   - Added PetState member and pet() accessor
   - Connected PetState to EventBus in constructor
   - Reset pet state on zone change and clearAll
   - Clear vendor/banker/trainer/loot state on zone change

6. **Build system** (`CMakeLists.txt`)
   - Added `src/client/state/pet_state.cpp` to sources

**Build verification:** ✅ Project compiles successfully with no errors

### Phase 2: Action System Updates - COMPLETED

**Status:** ✅ COMPLETE (2026-01-10)

**What was implemented:**

1. **IActionHandler interface** (`include/client/action/action_dispatcher.h`)
   - Added `sendPetCommand(uint8_t command, uint16_t targetId)` virtual method
   - Added `dismissPet()` virtual method
   - Added `clickWorldObject(uint32_t dropId)` virtual method
   - Added `tradeskillCombine()` virtual method

2. **ActionDispatcher** (`include/client/action/action_dispatcher.h`, `src/client/action/action_dispatcher.cpp`)
   - Added pet actions section with methods:
     - `sendPetCommand()` - Generic pet command
     - `dismissPet()` - Dismiss pet
     - `petAttack()` - Attack target (uses PET_ATTACK = 2)
     - `petBackOff()` - Stop attacking (uses PET_BACKOFF = 28)
     - `petFollow()` - Follow owner (uses PET_FOLLOWME = 4)
     - `petGuard()` - Guard location (uses PET_GUARDHERE = 5)
     - `petSit()` - Toggle sit (uses PET_SIT = 6)
     - `petTaunt()` - Toggle taunt (uses PET_TAUNT = 12)
     - `petHold()` - Toggle hold (uses PET_HOLD = 15)
     - `petFocus()` - Toggle focus (uses PET_FOCUS = 24)
     - `petHealth()` - Health report (uses PET_HEALTHREPORT = 0)
   - Added tradeskill actions:
     - `clickWorldObject()` - Interact with world object
     - `tradeskillCombine()` - Combine items
   - All pet actions validate pet existence via `m_state.pet().hasPet()`

3. **InputHandler** (`include/client/input/input_handler.h`)
   - Added `TogglePetWindow` to InputAction enum

4. **CommandProcessor** (`include/client/action/command_processor.h`, `src/client/action/command_processor.cpp`)
   - Added `/pet <command>` command with subcommands:
     - attack, back/backoff, follow/followme, guard/guardhere
     - sit, taunt, hold, focus, health, dismiss/getlost
   - Registered in "Pet" category

5. **EQActionHandler** (`include/client/eq_action_handler.h`, `src/client/eq_action_handler.cpp`)
   - Implemented `sendPetCommand()` - delegates to `m_eq.SendPetCommand()`
   - Implemented `dismissPet()` - sends PET_GETLOST command
   - Implemented `clickWorldObject()` - delegates to `m_eq.SendClickObject()`
   - Implemented `tradeskillCombine()` - placeholder (UI-driven)

**Build verification:** ✅ Project compiles successfully with no errors

### Phase 3: Optional State Classes - COMPLETED

**Status:** ✅ COMPLETE (2026-01-10)

**What was implemented:**

1. **TradeskillState class** (`include/client/state/tradeskill_state.h`, `src/client/state/tradeskill_state.cpp`)
   - World object container tracking: activeObjectId, containerName, containerType, slotCount
   - Inventory container tracking: activeInventorySlot
   - Methods: isContainerOpen(), isWorldContainer(), isInventoryContainer()
   - openWorldContainer() - Open forge, loom, etc.
   - openInventoryContainer() - Open tradeskill bag
   - closeContainer(), reset()
   - Events: TradeskillContainerOpened, TradeskillContainerClosed

2. **WorldState updates** (`include/client/state/world_state.h`, `src/client/state/world_state.cpp`)
   - Added isZoning() and setZoning() for active zone transition toggle
   - Added m_isZoning member variable
   - Reset zoning flag in resetZoneState()

3. **EventBus updates** (`include/client/state/event_bus.h`)
   - Added TradeskillContainerOpenedEvent and TradeskillContainerClosedEvent data structs
   - Added new event types to EventData variant

4. **GameState updates** (`include/client/state/game_state.h`, `src/client/state/game_state.cpp`)
   - Added TradeskillState member and tradeskill() accessor
   - Connected TradeskillState to EventBus in constructor
   - Reset tradeskill state on zone change and clearAll

5. **Build system** (`CMakeLists.txt`)
   - Added `src/client/state/tradeskill_state.cpp` to sources

**Build verification:** ✅ Project compiles successfully with no errors

### Phase 4: Inventory and Spell State - COMPLETED

**Status:** ✅ COMPLETE (2026-01-10)

**What was implemented:**

1. **InventoryState class** (`include/client/state/inventory_state.h`, `src/client/state/inventory_state.cpp`)
   - Equipment slot occupancy tracking (22 slots)
   - General inventory slot tracking (8 slots)
   - Bank slot tracking (16 slots)
   - Bag size tracking for each general slot
   - Cursor item state and queue size
   - Equipment stats summary (AC, ATK, HP, mana, weight)
   - Summary counts: equippedCount(), generalItemCount(), bankItemCount()
   - Events: InventorySlotChanged, CursorItemChanged, EquipmentStatsChanged
   - Methods: setEquipmentOccupied(), setGeneralOccupied(), setBankOccupied(), setCursorItem(), setEquipmentStats()

2. **SpellState class** (`include/client/state/spell_state.h`, `src/client/state/spell_state.cpp`)
   - Spell gem tracking (8 gems): spell ID, gem state, cooldown
   - Gem states: Empty, Ready, Casting, Refresh, MemorizeProgress
   - Casting state: spell ID, target ID, cast time remaining/total
   - Memorization state tracking
   - Spellbook summary: scribed spell count, tracked spells lookup
   - Events: SpellGemChanged, CastingStateChanged, SpellMemorizing
   - Methods: setGem(), setGemCooldown(), setCasting(), setMemorizing()

3. **EventBus updates** (`include/client/state/event_bus.h`)
   - Added event types: InventorySlotChanged, CursorItemChanged, EquipmentStatsChanged, SpellGemChanged, CastingStateChanged, SpellMemorizing
   - Added event data structs: InventorySlotChangedData, CursorItemChangedData, EquipmentStatsChangedData, SpellGemChangedData, CastingStateChangedData, SpellMemorizingData
   - Updated EventData variant with new types

4. **GameState updates** (`include/client/state/game_state.h`, `src/client/state/game_state.cpp`)
   - Added InventoryState member and inventory() accessor
   - Added SpellState member and spells() accessor
   - Connected both to EventBus in constructor
   - Reset inventory bank on zone change
   - Reset casting state on zone change
   - Full reset on clearAll

5. **Build system** (`CMakeLists.txt`)
   - Added `src/client/state/inventory_state.cpp`
   - Added `src/client/state/spell_state.cpp`

**Build verification:** ✅ Project compiles successfully with no errors

---

## Files to Modify

### New Files:
- ✅ `include/client/state/pet_state.h` - CREATED (Phase 1)
- ✅ `src/client/state/pet_state.cpp` - CREATED (Phase 1)
- ✅ `include/client/state/tradeskill_state.h` - CREATED (Phase 3)
- ✅ `src/client/state/tradeskill_state.cpp` - CREATED (Phase 3)
- ✅ `include/client/state/inventory_state.h` - CREATED (Phase 4)
- ✅ `src/client/state/inventory_state.cpp` - CREATED (Phase 4)
- ✅ `include/client/state/spell_state.h` - CREATED (Phase 4)
- ✅ `src/client/state/spell_state.cpp` - CREATED (Phase 4)

### Modified Files (Phase 1 - COMPLETE):
- ✅ `include/client/state/player_state.h` - Added bank/trainer/vendor/loot state
- ✅ `src/client/state/player_state.cpp` - Implemented new methods
- ✅ `include/client/state/entity_manager.h` - Added pet/weapon fields to Entity
- ✅ `src/client/state/entity_manager.cpp` - Added pet query methods
- ✅ `include/client/state/event_bus.h` - Added new event types
- ✅ `include/client/state/game_state.h` - Added pet() accessor
- ✅ `src/client/state/game_state.cpp` - Initialize PetState
- ✅ `CMakeLists.txt` - Added pet_state.cpp to sources

### Modified Files (Phase 2 - COMPLETE):
- ✅ `include/client/action/action_dispatcher.h` - Added pet/tradeskill actions to IActionHandler and ActionDispatcher
- ✅ `src/client/action/action_dispatcher.cpp` - Implemented all pet and tradeskill action methods
- ✅ `include/client/action/command_processor.h` - Added cmdPet declaration
- ✅ `src/client/action/command_processor.cpp` - Implemented /pet command with all subcommands
- ✅ `include/client/input/input_handler.h` - Added TogglePetWindow action
- ✅ `include/client/eq_action_handler.h` - Added pet/tradeskill method declarations
- ✅ `src/client/eq_action_handler.cpp` - Implemented pet/tradeskill methods

### Modified Files (Phase 3 - COMPLETE):
- ✅ `include/client/state/world_state.h` - Added isZoning() toggle
- ✅ `src/client/state/world_state.cpp` - Reset zoning in resetZoneState()
- ✅ `include/client/state/event_bus.h` - Added tradeskill event data structs
- ✅ `include/client/state/game_state.h` - Added tradeskill() accessor
- ✅ `src/client/state/game_state.cpp` - Initialize TradeskillState
- ✅ `CMakeLists.txt` - Added tradeskill_state.cpp to sources

### Modified Files (Phase 4 - COMPLETE):
- ✅ `include/client/state/event_bus.h` - Added inventory/spell event types and data structs
- ✅ `include/client/state/game_state.h` - Added inventory() and spells() accessors
- ✅ `src/client/state/game_state.cpp` - Initialize InventoryState and SpellState, handle resets
- ✅ `CMakeLists.txt` - Added inventory_state.cpp and spell_state.cpp to sources

---

## Testing Considerations

1. **Pet State Tests:**
   - Pet creation/removal events
   - Button state toggle
   - HP/mana updates

2. **Player State Tests:**
   - Bank currency tracking
   - Practice points decrement
   - NPC interaction state transitions

3. **Inventory State Tests:**
   - Slot occupancy changes fire events
   - Cursor item state transitions
   - Equipment stats updates
   - Bank slot reset on zone change

4. **Spell State Tests:**
   - Gem spell ID and state changes
   - Cooldown progress calculations
   - Casting state transitions
   - Memorization state tracking

5. **Integration Tests:**
   - Pet command → EQ class → state update → event fired
   - Zone line trigger → state update → zone change
   - Item pickup → inventory state → cursor event
   - Spell cast → spell state → casting event

---

## Notes

- The EQ class still owns the authoritative state during the transition period
- GameState provides a clean interface for renderers and mode controllers
- State synchronization happens after EQ class processes packets
- Events allow renderers to react to state changes without polling
