# D05: Publish Chat, Combat, and Player Stat Events

## Plan

### Overview

Add event publishing alongside existing direct renderer calls for chat messages,
combat/targeting events, and player stat updates.

### Steps

1. Publish ChatMessage event in `ZoneProcessChannelMessage()` (line ~7360):
   - After `chatWindow->addMessage(...)`, push ChatMessage with sender, message,
     channelType (EQ channel uint32), channelName (derived from channel type)

2. Publish SystemMessage event in `AddChatSystemMessage()` (line ~7420):
   - After `chatWindow->addSystemMessage(text)`, push ChatMessage with empty sender,
     message text, and system channel type
   - Note: uses ChatMessageData for both ChatMessage and SystemMessage event types

3. Publish ChatMessage event in `AddChatCombatMessage()` (line ~7440):
   - After `chatWindow->addSystemMessage(text, channel)`, push ChatMessage with
     combat channel type

4. Publish ChatMessage event in `AddChatMissMessage()` (line ~7465):
   - After `chatWindow->addSystemMessage(text, CombatMiss)`, push ChatMessage with
     combat miss channel type

5. Publish TargetChanged event in target selection callback (line ~18326):
   - After `m_renderer->setCurrentTargetInfo(info)`, push TargetChanged with spawnId
   - Note: this is inside a lambda set by setTargetCallback

6. Publish TargetChanged (clear) in `OnSpawnRemovedGraphics()` (line ~19661):
   - After `m_renderer->clearCurrentTarget()`, push TargetChanged with spawnId=0

7. Publish EntityStatsChanged in HP percent update handler (line ~7138):
   - After `m_renderer->updateCurrentTargetHP(hp_percent)`, push EntityStatsChanged
   - This is the real-time HP update for the entity being targeted

8. Publish PlayerStatsChanged in `ZoneProcessHPUpdate()` (line ~7200):
   - After player HP update (is_self path), push PlayerStatsChanged

9. Publish PlayerStatsChanged in `ZoneProcessManaChange()` (line ~12674):
   - After mana/endurance update, push PlayerStatsChanged

10. Publish PlayerStatsChanged in `UpdateInventoryStats()` (line ~19855):
    - After `m_renderer->updateCharacterStats(...)`, push PlayerStatsChanged

11. Publish ExpProgressChanged in `ZoneProcessExpUpdate()` (line ~6677):
    - After `m_renderer->setExpProgress(...)`, push ExpProgressChanged

12. Publish DamageEvent in damage handler (line ~17860):
    - After damage processing, push DamageEvent with source/target/amount/type
    - Separate from combat animation (D04) — this is the damage DATA event

13. Build and verify compilation

## Acceptance Criteria

- All existing direct renderer calls remain unchanged
- Bridge receives chat, combat, and player stat events
- All push calls guarded by `if (m_bridge)`
- No new warnings or errors in build
- Existing tests pass unchanged

## Review

All steps completed as planned. No deviations.

Call sites covered:
- Chat: ZoneProcessChannelMessage (incoming chat), AddChatSystemMessage,
  AddChatCombatMessage, AddChatMissMessage
- Target: target selection callback (TargetChanged), OnSpawnRemovedGraphics
  (TargetChanged clear)
- Entity stats: HP percent update (EntityStatsChanged)
- Player stats: ZoneProcessHPUpdate, ZoneProcessManaChange,
  UpdateInventoryStats (PlayerStatsChanged × 3 sites)
- Exp: ZoneProcessExpUpdate (ExpProgressChanged)
- Damage: ZoneProcessDamage (DamageEvent, data only — animation was D04)

TargetChanged uses CombatEventData (reusing existing variant type with
sourceId/targetId fields). SystemMessage uses ChatMessageData with
channelName="system".

Build succeeds, 82 relevant tests pass. All acceptance criteria met.
