# D02: Define Renderer Intent Types

## Plan

### Steps

1. Create `include/client/events/renderer_intents.h` with all intent structs
   and a `RendererIntent` variant type.

2. Intent categories (from Plan D):
   - Movement: `PlayerPositionChanged`
   - Targeting: `TargetIntent`
   - Combat: `ToggleAutoAttackIntent`, `AttackIntent`
   - Chat: `ChatSubmitIntent`
   - Interaction: `DoorInteractIntent`, `LootCorpseIntent`, `LootItemIntent`,
     `LootAllIntent`, `DestroyAllLootIntent`, `CloseLootIntent`,
     `WorldObjectInteractIntent`, `ZoningEnabledIntent`, `ReadItemIntent`
   - Vendor: `VendorToggleIntent`, `VendorBuyIntent`, `VendorSellIntent`,
     `CloseVendorIntent`
   - Bank: `BankerInteractIntent`, `BankCurrencyMoveIntent`,
     `BankCurrencyConvertIntent`, `CloseBankIntent`
   - Trade: `TradeRequestIntent`, `TradeAcceptIntent`, `TradeCancelIntent`
   - Trainer: `TrainerToggleIntent`
   - Spells: `CastSpellIntent`, `MemorizeSpellIntent`, `ForgetSpellIntent`,
     `ScribeSpellIntent`, `SpellbookStateIntent`, `InterruptSpellIntent`
   - Buffs: `BuffCancelIntent`
   - Skills: `SkillActivateIntent`
   - Pet: `PetCommandIntent`
   - Group: `GroupInviteIntent`, `DisbandIntent`, `DeclineInviteIntent`
   - General: `RequestCampIntent`, `RequestQuitIntent`, `SlashCommandIntent`,
     `RequestMemoryReport`, `RequestSceneDump`, `HotbarChangedIntent`

3. No behavioral changes — just the header file + CMakeLists update if needed
   (header-only, so likely no CMake change).

4. Build and verify compilation.

### Relationship to ActionDispatcher

- `ActionDispatcher` remains the renderer-internal action system with validation
- `InputActionBridge` stays inside the renderer; converts input → actions
- `RendererIntent` is the cross-thread type for the bridge queue (D03)
- In later phases (D14-D16), ActionDispatcher will push intents to bridge
  instead of calling IActionHandler directly

## Acceptance Criteria

- Header compiles with no renderer includes and no EverQuest includes
- All structs are default-constructible and movable
- `RendererIntent` variant can hold any intent type
- No existing code modified (except CMakeLists if needed)
- Build succeeds, all tests pass

## Review

All steps completed as planned. No deviations.

- Created `include/client/events/renderer_intents.h` with 42 intent structs
  and `RendererIntent` variant covering all categories from Plan D.
- Header-only, no CMakeLists changes needed.
- Verified standalone compilation with g++ test.
- No renderer or EverQuest includes.
- Build succeeds, 1201/1201 tests pass.
- All acceptance criteria met.
