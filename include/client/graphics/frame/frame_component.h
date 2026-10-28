#ifndef EQT_GRAPHICS_FRAME_FRAME_COMPONENT_H
#define EQT_GRAPHICS_FRAME_FRAME_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L11 -- the frame
// family lifted out of class IrrlichtRenderer: the per-frame loop, the input
// drain and the static UI panel state, across three TUs:
//
//   frame/renderer_frame.cpp         processFrame(), the four-phase
//       orchestrator, plus the frame-timing accumulation and the spike/RED/
//       budget diagnostics it emits.
//   frame/renderer_frame_input.cpp   phase 1 (processFrameInput,
//       processCommonInput, processPlayerInput, processInputDeltas,
//       processChatInput), phase 2 (processFrameVisibility) and phase 3
//       (processFrameSimulation).
//   frame/renderer_frame_render.cpp  phase 4 (processFrameRender).
//
// Every body is the body that used to live in
// src/client/graphics/renderer_frame{,_input,_render}.cpp, moved verbatim.
// Only the directory moved; all three basenames are unchanged, because
// tests/test_ui_hide_command.cpp and tests/test_static_ui_scroll.cpp read two
// of them AS TEXT and docs/module_map.md lists all three.
//
// The only edits inside a body are the mechanism's substitutions: a core
// member becomes ctx_.member, a non-core IrrlichtRenderer member becomes
// owner_.member_, a method that stayed behind becomes owner_.Method(), a
// sibling component pointer becomes owner_.<Family>(), and an
// IrrlichtRenderer-scoped nested type named in a body becomes
// IrrlichtRenderer::Type (batch corrections 38/41 -- here exactly once,
// FrameTimings() in processFrame's accumulator reset).
//
// THREADING -- NOTHING CHANGED, AND NOTHING HERE OWNS A THREAD.
// This is the render/game thread boundary, so it is the one place in Batch L
// where the STOP condition had to be checked line by line rather than
// asserted. The shape, unchanged by this unit:
//
//   * processFrame() runs on the RENDER thread. Its two production callers
//     are Application::render() (src/client/application_frame.cpp:181,
//     through the forwarder kept on IrrlichtRenderer) and
//     IrrlichtRenderer::run() (irrlicht_renderer.cpp), which is the render
//     thread's own loop. Every phase below it runs on that same thread, in
//     the same order, with the same early-returns.
//   * processFrameInput() drains RendererEventReceiver's render-thread
//     queues (drainActions, wasLeftButtonClicked, getMouseWheelDelta, the
//     key events) exactly as before. eventReceiver_ itself is a
//     RendererContext core member and did NOT move; the receiver's own
//     mutex-guarded bridgeQueue_ (T312) is untouched, and this component
//     never reaches into it except through the receiver's public API.
//   * processChatInput() is the render-thread end of the T319 prefill trip.
//     The parked text lives in IrrlichtRenderer::pendingChatPrefill_ under
//     IrrlichtRenderer::chatPrefillMutex_ and is taken through
//     owner_.takeChatPrefill(). BOTH the member and the mutex stayed on
//     IrrlichtRenderer; the accessor pair is the only access path, exactly as
//     the header there requires. L07 correction 31 for the fifth time: owner_
//     is the ANSWER to a thread-ownership question, not a workaround for one.
//   * processFrameSimulation()/processFrame() talk to the SimulationWorker
//     only through owner_.SimBridge() (applySimulationResults,
//     postSimulationInput), whose worker handle stayed on IrrlichtRenderer at
//     L07. No synchronisation primitive, no queue, no buffer lifetime and no
//     thread ownership changed anywhere in this unit.
//
// The UI PANEL STATE below is written by the GAME thread through
// src/client/bridge/irrlicht_bridge.cpp and read by the RENDER thread here,
// and that has always been true -- these sixteen members were already PUBLIC
// on IrrlichtRenderer with the comment "Public so IrrlichtBridge can update
// them directly". Moving them changes the receiver expression and nothing
// else: same objects, same lifetime (a plain member of IrrlichtRenderer),
// same absence of a lock, same threads. Five more that were private
// (uiLayout_, uiRenderer_, newUIEnabled_, allUIHidden_, staticUIInput_) are
// public here for the same reason -- their external reachers are
// renderer_init.cpp, SlashCommandsComponent and DiagnosticsComponent, and a
// public member keeps every one of those a pure receiver retarget. THAT IS
// THE SMALLEST PUBLIC SURFACE AVAILABLE: it adds no new named API at all,
// where an accessor pair per member would have added ten method names (and,
// for the three unique_ptr members that renderer_init.cpp assigns into, would
// have had to hand out a reference to the smart pointer itself).
//
// FORWARDERS on IrrlichtRenderer, each justified by a NAMED caller and never
// by an interface (batch corrections 20/23/37 -- IrrlichtRenderer's only base
// is IZoneLoadSurface, whose two methods are initLoadingScreen() and
// initDeferredGraphics(), neither of them in this move set):
//
//   processFrame()        src/client/application_frame.cpp:181,
//                         src/client/graphics/renderer/irrlicht_renderer.cpp (run()),
//                         tests/test_zoning_graphics_integration.cpp (4),
//                         tests/test_beneficial_spell_casting.cpp (2)
//   isAllUIHidden()       inline accessor on a MOVING member (K03 (d) /
//                         K04 (f)); callers in slash/ and two tests
//   isNewUIEnabled()      inline accessor on a MOVING member; slash/
//   setNewUIEnabled(bool) inline accessor on a MOVING member
//   getUIRenderer()       inline accessor on a MOVING member
//
// run() and shutdown() stay on IrrlichtRenderer (core, not frame). So does
// measureSection(): L04 recorded it as frame-phase state whose body was
// relocated into this family's TU without moving its ownership, and this unit
// leaves that decision alone -- sectionStart_ and the body stay on
// IrrlichtRenderer, in frame/renderer_frame.cpp, and the 35 call sites in the
// moved bodies reach it as owner_.measureSection().
//
// MEMBERS: 31 moved (21 UI panel/static-UI state, 10 private frame-loop
// state) plus the two tier-interval constants; 42 reached through
// RendererContext; the rest -- frameTimings_/frameTimingsAccum_ and their two
// bookkeeping members above all (shared with DiagnosticsComponent, 93
// references there) -- reached through owner_.
//
// SIBLING COMPONENTS reached across the boundary through IrrlichtRenderer's
// accessors (K06/K07 correction (r)'s already-on-fable form):
//   owner_.Overlays()      handleMouseTargeting, clearCurrentTarget,
//                          toggleZoneLineVisualization, toggleMapOverlay,
//                          toggleNavmeshOverlay, updateHUD, drawLoadingScreen,
//                          drawTargetOutline/drawTargetSelectionBox,
//                          updateMapOverlay/drawMapOverlay,
//                          updateNavmeshOverlay/drawNavmeshOverlay,
//                          drawFPSCounter, updatePaperdollForFrame,
//                          drawZoneLineBoxLabels
//   owner_.Diagnostics()   toggleAllUI, toggleZoneLights, cycleObjectLights,
//                          toggleWireframe, toggleHUD, toggleNameTags,
//                          toggleOldModels, logFrameTimings,
//                          profileSceneBreakdown
//   owner_.PlayerCamera()  updatePlayerMovement, applyFirstPersonPitchClamp,
//                          drawCollisionDebugLines
//   owner_.SimBridge()     applySimulationResults, postSimulationInput
//   owner_.ZoneStream()    processFrameProgressiveLoad, queueEntityPrepRequests
//   owner_.ZoneLifecycle() processCompletedUploads, drainGPUResults,
//                          processOneGPUResult, buildZonePlaceholder,
//                          setupHCMapCollision
//   owner_.Eqe1()          updateFireGlowLighting, renderFireGlowIcospheres
//   owner_.Rdp()           captureFrameForRDP
//
// SIX IrrlichtRenderer METHODS THAT STAYED are reached as owner_.X():
// isRunning(), measureSection(), takeChatPrefill(), getNearestWorldObject(),
// toggleInventory() and saveScreenshot().
//
// The component needs friendship on BOTH axes of K13 correction (ii): axis 1,
// it reaches ~60 private IrrlichtRenderer members and the six private methods
// above through owner_; axis 2, processFrame() names the private nested type
// IrrlichtRenderer::FrameTimings when it resets the accumulator.

#include "client/graphics/renderer_context.h"
#include "client/graphics/entity_build_scheduler.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/friends_panel.h"  // B897
// T436 (board B429): the merchant, bank and cursor-attachment windows.
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/tracking_panel.h"  // B895
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/trainer_panel.h"  // B898
#include "client/graphics/ui/container_panel.h"
#include "client/graphics/ui/trade_panel.h"  // B896
#include "client/graphics/ui/esc_window_stack.h"  // B460
#include "client/graphics/ui/tip_window.h"  // B834
#include "client/graphics/ui/item_tooltip.h"  // B1004
#include "client/graphics/ui/ui_preferences.h"
#include "client/graphics/ui/eqclient_ini.h"  // B1175

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eqt { namespace ui { class ChatMessageBuffer; } }

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;
class UIRenderer;
class StaticUIInput;

class FrameComponent {
public:
	FrameComponent(RendererContext& ctx, IrrlichtRenderer& owner);
	~FrameComponent();

	FrameComponent(const FrameComponent&) = delete;
	FrameComponent& operator=(const FrameComponent&) = delete;

	// The per-frame entry point. Returns false when the renderer should stop.
	// Reached from production through IrrlichtRenderer::processFrame().
	bool processFrame(float deltaTime);

	// Phase 1 sub-steps that tests drive directly (each on a bare,
	// never-Init()'d renderer -- the T66/T233/T235 no-device idiom):
	//   processCommonInput  tests/test_ui_hide_command.cpp,
	//                       tests/test_debug_hotkey_arms.cpp
	//   processPlayerInput  tests/test_inventory_toggle.cpp
	//   processChatInput    tests/test_spell_gem_key_race.cpp,
	//                       tests/test_reply_to_tell_key.cpp
	void processCommonInput(const std::vector<RendererEvent>& actions);
	void processPlayerInput(const std::vector<RendererEvent>& actions);
	void processChatInput();

	/**
	 * T421 (board B365): the tradeskill search boxes' arm of the key drain.
	 *
	 * processChatInput() owns the keyboard gate for the WHOLE renderer (it
	 * is the only caller of setChatInputFocused()), so a second focusable
	 * field has to be routed from there rather than from StaticUIInput --
	 * which has no event receiver and cannot open the gate. Returns true if
	 * the tradeskill window holds the keyboard this frame, in which case
	 * chat does not see the keys.
	 */
	bool processTradeskillSearchInput();

	// ---- T363 (B058): the ONE skills-window toggle ----
	// Both reachable paths land here: the `/skills` slash command (registry 3,
	// which piped stdin's bare `skills` word also reaches -- see
	// ConsoleInputHandler) and the K keybind (HotkeyAction::ToggleSkills ->
	// RendererAction::ToggleSkills -> processPlayerInput's arm). Before this
	// they were two copies and only the slash one closed the other two centre
	// popups, so pressing K left the spellbook stacked underneath. Same shape
	// as T294's IrrlichtRenderer::toggleInventory(). Returns the new open
	// state so the slash arm can echo it without re-reading the member.
	bool toggleSkillsWindow();

	// ---- B1077 (GitLab #397): the ONE spellbook toggle ----
	// Three reachable paths, one body: the Ctrl+B keybind
	// (RendererAction::ToggleSpellbook), the /newbook slash command, and the
	// gem window's own CSPW_SpellBook button (the oracle's 22x22 toggle at
	// (10,252); spellGemState_.bookClicked, consumed in processPlayerInput).
	// Before this, the keybind arm closed NOTHING and the slash arm closed
	// the inventory per the co-open table -- two bodies, two behaviours.
	// The kept body is the slash one: B1075's closesOnOpen() says opening
	// the spellbook closes the inventory (no shared required set), and the
	// guard is on the OPEN branch only, exactly toggleSkillsWindow().
	// Returns the new open state so the slash arm can echo it.
	bool toggleSpellbookWindow();

	// ---- T368 (B101): the three keys that used to reach empty arms ----
	// The group, pet and buff keys (Alt+P / Alt+F1 / Alt+B since T382's B170
	// re-measure) each reached a `break;` with
	// the comment "visibility is driven by <auto> state", so the key did
	// nothing at all -- a bound key that advertises a behaviour the tree does
	// not have. Each now flips an `isOpen` its panel honours, exactly the
	// shape toggleSkillsWindow() has, and each returns the new state.
	//
	// The official client's on-disk files name these three actions
	// TOGGLE_PARTYWIN, TOGGLE_PETINFOWIN and TOGGLE_BUFFWIN (the KEYMAPPING_
	// suffixes in eqgame.exe) and record their windows as shown by default
	// (defaults.ini writes `Show=0` only for a window whose default is
	// closed). They do NOT record the default KEY for any of the three -- an
	// eqclient.ini carries a KEYMAPPING_ line only for a binding the user
	// changed. T377 got them off the running client instead, by screenshotting
	// its Options > Keys > UI page (review/T377/oracle/): Alt+P, Alt+F1 and
	// Alt+B, which T382 wrote into registerDefaultBindings. B115 and B170 are
	// closed.
	//
	// Composition with the auto-driven state (see static_panels.h):
	//   group  -- toggle only; an open window with no group draws its bare
	//             title bar (T382, measured), and joining a group does not
	//             reopen a closed one
	//   pet    -- toggle AND hasPet
	//   buffs  -- toggle AND the bar having at least one buff
	bool toggleGroupWindow();
	bool togglePetWindow();
	bool toggleBuffWindow();

	// ---- T415 (board B320): the combat abilities window's toggle ----
	// MEASURED at the official client's Options > Keys > UI page (T377,
	// review/T377/oracle/T377_oracle_options_keys_UI_page3.png):
	// "Toggle Combat Ability Window   Alt C", Alternate column empty. Same
	// shape as the three above -- a HUD window, not a centre popup, so it is
	// deliberately NOT mutually exclusive with the inventory/spellbook/skills
	// popups. Returns the new open state.
	bool toggleCombatAbilityWindow();

	// B1025-H4 (#345): "Toggle Hotbar 1 window" (eqstr 6587), Alt+H (H-O,
	// pages.tsv:2, 27_keys_hb1.png). The hotbar window's show/hide toggle --
	// the same flip the panel's close box does on click, on
	// hotbarState_.isOpen. Returns the new open state.
	bool toggleHotbar1Window();

	// ---- T434: the Options window's toggle ----
	// Before this the ONLY arm for the bound key was a bare `break;` in
	// processCommonInput() with the comment "WindowManager removed -- options
	// window not available", and a `PopupType` enumerator named Options had
	// zero uses in the tree: T368's dead-toggle shape, on the one window that
	// is the home of every user-changeable setting. T439 deleted that
	// enumerator (board B434) -- this window is independent, not a centre
	// popup.
	//
	// The key is Alt+O, MEASURED at the official client's Options > Keys > UI
	// page (review/T377/oracle/T377_oracle_options_keys_UI_page2.png:
	// "Toggle options window   Alt O", Alternate column empty). fable had a
	// bare `O`, an invention of the same class as the `G`/`P` T382 removed.
	//
	// A HUD-style window with its own oracle placement, NOT a centre popup,
	// so opening it closes nothing. Returns the new open state.
	bool toggleOptionsWindow();

	// The one place a wired Options control reaches its setting. Two of the
	// three need a bridge intent and the third a renderer call, and all three
	// need the DELTA against what was last applied -- which is why
	// StaticUIInput resolves the click into a Setting and this applies it.
	// Called from renderStaticUI() when processOptionsInput() reports a
	// change, and from syncUiPreferences() after a load.
	void applyOptionsSetting(OptionsLayout::Setting setting);
	// B430: the arms for every setting added after T434 (same TU), and the
	// load path's "tell everything" -- every persisted setting once.
	void applyOptionsSettingB430(OptionsLayout::Setting setting);
	void applyAllOptionsSettings();
	// B430/B433: `/option` and `/filter` (registry 3). Runs the grammar in
	// options_command.cpp against optionsState_, applies every setting it
	// changed through applyOptionsSetting(), and returns the reply lines.
	std::vector<std::string> runOptionsCommand(const std::string& cmd,
	                                           const std::string& args);
	// B995: the Display > Particles pages as the particle systems' knobs
	// (particleKnobsFromOptions over optionsState_). The sim bridge hands it
	// to ParticleManager and the worker every frame.
	Environment::ParticleKnobs particleOptionKnobs() const;
	// B1165 (#484): the Spell page + ShowSpellEffects for SpellVisualFX
	// (spellFxOptionsFromOptions over optionsState_), handed over every frame.
	EQ::SpellFxOptions spellFxOptions() const;
	// B433: the Keys page's key capture (renderer_frame_options.cpp); called
	// first thing in processChatInput(), true while it owns the keyboard.
	bool processOptionsKeyCapture();
	bool optionsKeyCaptureGate_ = false;

	// ---- B897 (GitLab #213): the Friends window, renderer_frame_friends.cpp ----
	// toggleFriendsWindow(): Alt+F (MEASURED: review/T377/oracle/
	// T377_oracle_options_keys_UI_page1.png, "Toggle friend window  Alt F",
	// Alternate empty -- eqstr 6599) and /friendswindow. A HUD-style window
	// with its own oracle placement, so it closes nothing. Returns the new
	// open state.
	bool toggleFriendsWindow();
	// The click pass, from renderStaticUI(): resolve, apply, act.
	void processFriendsWindowInput();
	// The Editbox key gate, from processChatInput() beside the tradeskill
	// one: true while FW_NameInput / FW_IgnoreNameInput holds the keyboard.
	bool processFriendsNameInput();
	// One resolved click -> a SocialListIntent, the chat prefill, or a close.
	void applyFriendsAction(const FriendsAction& action);

	// B442b (GitLab #151), renderer_frame_cursor.cpp. Once per frame from
	// renderStaticUI(), after every window had the click (`uiTook` says
	// whether one of them took it): the motionless world click with an item
	// held (rows 31-33), and the item display's held/kept requests and the
	// right button's release (rows 24-26).
	void processCursorGestureFrame(bool uiTook);
	// The QuantityWnd box's key gate (row 5), from processChatInput() beside
	// the friends one: true while QTYW_SliderInput holds the keyboard.
	bool processQuantityKeyInput();
	// True when a static window's own geometry holds (x,y) but its entry
	// point returns nothing (the Friends window, the two dialogs).
	bool pointOverSelfConsumingWindow(int x, int y) const;
	// B1066 (#386): true when a press at (x,y) belongs to the static UI --
	// an entry point took the position (uiTook), or a window whose entry
	// point reports nothing holds it. False while the UI is hidden.
	bool pointerOwnedByUI(bool uiTook, int x, int y) const;
	// ---- B896 (GitLab #212): the trade / give window, renderer_frame_trade.cpp ----
	// The click pass, from renderStaticUI() right after the QuantityWnd: the
	// window is drawn above the centre popups, the bank, the trainer and the
	// container windows, and the inventory is always open beside it (the
	// official client opens both, trade.tsv row 1), so a click it takes is
	// withheld from every window below it.
	void processTradeWindowInput();
	// ---- B460 (GitLab #156): ESC closes the most recently opened window ----
	// observeEscWindows() runs every frame (processFrameInput) so the stack
	// sees every opener; handleEscapeKey() is the ESC press, from
	// processChatInput(). Returns true when a window took the press (false:
	// it cleared the target, or there was nothing to do).
	std::vector<EscWindowKey> openEscWindows() const;
	void observeEscWindows();
	bool handleEscapeKey();
	EscWindowStack escWindowStack_;

	// ---- B834 (GitLab #202): the tip windows, renderer_frame_tips.cpp ----
	// updateTipWindows() runs once a frame: it pops the Tip of the Day after
	// the first zone-in of a character and a context tip on the rising edge
	// of each window that has one (tip_window.h lists the oracle and the
	// flagged assumptions). processTipWindowInput() is the click pass, true
	// when the window took the click. The texts are read lazily from the
	// client's own help/tips/ under RendererConfig::eqClientPath.
	void updateTipWindows();
	bool processTipWindowInput();
	std::set<std::string> activeTipTriggers() const;
	const TipDocument* tipDocument(const std::string& key);
	TipWindowState tipState_;
	TipPreferences tipPrefs_;
	std::vector<TipDocument> totdEntries_;
	bool totdLoaded_ = false;
	std::map<std::string, TipDocument> tipDocs_;
	std::set<std::string> tipTriggersLast_;

	// ---- B1004 (GitLab #323): item-name tooltips, renderer_frame_tooltips.cpp ----
	// Alt held over a bag / the bank: a name label on every occupied slot;
	// a resting pointer: the hover tooltip (item_tooltip.h has the measured
	// rule). Drawn by renderStaticUI() as its own top-most layer, below only
	// the cursor attachment.
	void renderItemTooltipLayer();
	ItemTooltipState itemTooltipState_;

	// ---- T432 (board B275): the ONE per-character persistence path ----
	// Before this the client persisted NOTHING a player changed. Will's
	// ruling (docs/decisions.md 2026-09-07) is to persist per character what
	// is user-changeable: the window open/closed states and the camera mode.
	//
	// setUiPreferencesIdentity() is called once per zone-in by Application,
	// which is where the character name (the zone-load snapshot) and the
	// server name (the config) are both known; syncUiPreferences() is called
	// once per frame from processFrame() and once from IrrlichtRenderer::
	// shutdown(), i.e. on the /camp path.
	//
	// There is deliberately no per-toggle save hook. sync() COMPARES the
	// current state against the last state it wrote and saves on a
	// difference, so a new persisted field cannot be added to the file and
	// then forgotten at one of nine call sites -- the single comparison sees
	// every change by construction. The compare is eight scalars; the write
	// happens only when the player actually toggled something.
	void setUiPreferencesIdentity(const std::string& character,
	                              const std::string& server);
	void syncUiPreferences();
	// B1175 (#494): the GLOBAL eqclient.ini [Defaults] (the particle keys and
	// ShowSpellEffects, eqclient_ini.h): load-then-apply on the first call,
	// save-on-difference after. Not per character, so it needs no identity;
	// syncUiPreferences() calls it first, on both of its call sites.
	void syncEqClientIni();
	// The binding over this component's own panel members. Public so a test
	// can build the identical one over plain structs.
	UiPreferencesBinding uiPreferencesBinding();

	// ---- T357 (B057): slash-command tab completion ----
	// The chat box is a hand-rolled string in chatPanelState_, drained by
	// processChatInput() on the RENDER thread; the command vocabulary lives on
	// the GAME thread (eqt::ui::CommandRegistry, which the submit path's
	// ChatSubmitIntent reaches through ChatComponent::DispatchSlashCommand,
	// plus action::CommandProcessor, which the unrecognized-command fallback
	// reaches). Rather than give the renderer either registry, Application
	// installs ONE provider closure here in initRendererAndGraphics() -- the
	// same merged vocabulary /help lists (eqt::help::BuildHelpResponse takes
	// both), so what Tab completes and what /help prints can never drift.
	//
	// Both maps are filled during EverQuest construction / initActionLayer(),
	// before the render loop starts, and nothing registers or unregisters a
	// command afterwards, so this const read needs no lock. If a command is
	// ever registered live, this becomes a race and needs one.
	using CommandCompletionProvider =
	    std::function<std::vector<std::string>(const std::string& partial)>;
	void setCommandCompletionProvider(CommandCompletionProvider provider) {
		commandCompletionProvider_ = std::move(provider);
	}
	bool hasCommandCompletionProvider() const {
		return static_cast<bool>(commandCompletionProvider_);
	}

	// ---- UI panel state ----
	// The first sixteen were already public on IrrlichtRenderer, with the
	// comment "Public so IrrlichtBridge can update them directly"; the game
	// thread writes them through src/client/bridge/irrlicht_bridge.cpp and
	// this component's phase-4 body reads them. Nothing about that changed.
	PlayerStatsData cachedPlayerStats_;
	TargetInfoData cachedTargetInfo_;
	ChatPanelState chatPanelState_;        // U04
	std::unique_ptr<eqt::ui::ChatMessageBuffer> newUIChatBuffer_;  // U04
	InventoryPanelState inventoryState_;  // U05
	HotbarPanelState hotbarState_;       // U06a
	SpellGemPanelState spellGemState_;  // U06b
	BuffBarState buffBarState_;         // U06c
	CastingBarState castingBarState_;  // U06d
	GroupPanelState groupPanelState_;  // U06e
	PetPanelState petPanelState_;       // U06f
	SpellbookPopupState spellbookState_; // U06g
	SkillsPopupState skillsPopupState_; // U06h
	// T290: renderer-side corpse contents, restored. Fed only by the four
	// loot bridge arms (applyLoot*), read by renderLootPopup() and
	// StaticUIInput's row/button hit tests.
	LootPanelState lootState_;          // T290
	// T415 (board B321): renderer-side tradeskill container contents. Fed by
	// the two Tradeskill* bridge arms plus the WORLD-slot branch of the
	// InventorySlotChanged arm; read by renderTradeskillPopup() and its two
	// hit tests.
	TradeskillPanelState tradeskillState_;      // T415
	// T415 (board B320): the combat abilities window. Its gauge is the only
	// consumer of SkillActivationFeedbackData::cooldownMs.
	CombatAbilityPanelState combatAbilityState_;  // T415
	// T421 (board B298, item half): the item display window. Fed by the
	// ItemDisplayShown bridge arm (whose lines the GAME thread composes with
	// BuildItemDisplayText), read by renderItemDisplayPopup() and
	// StaticUIInput::processItemDisplayInput(). Not one of the mutually
	// exclusive centre popups -- the oracle gives it its own absolute
	// placement -- so it is drawn on top of them.
	ItemDisplayPanelState itemDisplayState_;    // T421
	// T434: the Options window. Not one of the mutually exclusive centre
	// popups -- the oracle's OptionsWindow is `RelativePosition false` at its
	// own <Location> -- so it is drawn and hit-tested on top of them.
	OptionsPanelState optionsState_;            // T434
	// B897: the Friends window. Not a centre popup (RelativePosition false,
	// its own placement), drawn and hit-tested just below the Options window.
	// Fed by the SocialListsChanged bridge arm; persisted open/closed as
	// `[FriendsWindow] Show=` through UiPreferences.
	FriendsPanelState friendsState_;            // B897
	// T436 (board B429): the merchant window. A centre popup (the oracle's
	// MerchantWnd is Style_Sizable true and does not fit 640x480, so it is
	// scaled onto the 400x350 box the way T415 scaled the tradeskill window);
	// since B1075 that box is the layout table's PairLeft popup, beside the
	// inventory -- inventory+merchant is a required co-open set. Fed by the
	// three Vendor* bridge arms.
	MerchantPanelState merchantState_;          // T436
	// T436 (board B429): the bank window. NOT a centre popup -- the oracle's
	// BankWnd is Style_Sizable false at 108x347, so it keeps its own size and
	// its own anchor. Fed by the two Bank* window arms, BankCurrencyChanged
	// and the 2000..2007 branch of the InventorySlotChanged arm.
	BankPanelState bankState_;                  // T436
	// T436 (board B429): the item held on the cursor, drawn at the mouse.
	// Fed by the slot-30 branch of the InventorySlotChanged arm; its mouse
	// position is written once per frame by renderStaticUI().
	CursorAttachmentState cursorState_;         // T436
	// B898 (#214): the skill trainer window. NOT a centre popup -- the
	// oracle's TrainWindow is Style_Sizable false at 580x280. Fed by the
	// TrainerWindowOpened/Closed bridge arms.
	TrainerPanelState trainerState_;            // B898
	// B895 (GitLab #211): the Tracking Window. Not a centre popup (the
	// oracle's TrackingWnd has its own placement); drawn above the bank.
	// Fed by the TrackingListChanged bridge arm.
	TrackingPanelState trackingState_;          // B895
	// B899 (#215 / #151 Leg B): the era container windows -- the world
	// container (forge, oven) AND every carried bag. Fed by the two
	// TradeskillContainer* arms and the world/bag branch of the
	// InventorySlotChanged arm; NOT a centre popup (EQUI_Container.xml is
	// RelativePosition false, Style_Sizable false at 100x366).
	ContainerWindowsState containerState_;      // B899
	// B896 (GitLab #212): the trade window (EQUI_TradeWnd.xml) or the NPC
	// give window (EQUI_GiveWnd.xml), whichever the session is. Fed by the
	// seven Trade* bridge arms and the 3000..3007 branch of the
	// InventorySlotChanged arm; NOT a centre popup (RelativePosition false at
	// its own <Location>, and the inventory is open beside it).
	TradePanelState tradeState_;                // B896
	XPBarState xpBarState_;             // U06i
	// T432 (board B275): per-character UI persistence. Empty identity =
	// not logged in yet, and sync() is then a no-op.
	std::string uiPrefsCharacter_;
	std::string uiPrefsServer_;
	UiPreferences uiPrefsLastSaved_;
	bool uiPrefsLoaded_ = false;
	// T467 (board B560): set by an identity change FROM a character, consumed
	// by the next load -- every persisted field goes back to its default
	// before the new character's file is read.
	bool uiPrefsResetPending_ = false;
	// B1175: the global eqclient.ini's baseline (syncEqClientIni()).
	EqClientIni eqclientLastSaved_;
	bool eqclientLoaded_ = false;
	// T439 (board B432 CLOSED): optionsAppliedMusicStep_ /
	// optionsAppliedSoundStep_ lived here -- the steps the audio manager was
	// last TOLD, kept because the volume intents carried a DELTA and
	// applying a slider needed the previous value. Both are DELETED. The
	// intents carry an absolute 0..20 step now and the audio manager
	// publishes what it holds back on GameEventType::VolumesChanged, so
	// optionsState_ IS the record and nothing shadows it.
	std::unique_ptr<StaticUIInput> staticUIInput_;  // U06j

	// The five that were private on IrrlichtRenderer. Public here so that
	// renderer_init.cpp (which constructs uiRenderer_/staticUIInput_/
	// newUIChatBuffer_ and computes the layout), SlashCommandsComponent
	// (/newui, /scroll) and DiagnosticsComponent (toggleAllUI) each stay a
	// pure receiver retarget rather than needing an accessor pair.
	std::unique_ptr<UIRenderer> uiRenderer_;  // U03b
	UILayout uiLayout_;                       // U03a
	bool newUIEnabled_ = true;                // U07c1: new UI on by default
	bool allUIHidden_ = false;


	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): members that moved here from
	// class IrrlichtRenderer because THIS component is their owner under the
	// pairwise rule (Batch K Mechanism 3 applied to a two-component member: it
	// moves to the component that WRITES it, or on a tie to the one with more
	// uses). Every other reader reaches them through ctx_.Frame().<accessor>();
	// the renderer's own init/teardown reaches them through Frame().
	// Each accessor below is the SMALLEST the other side needs: a const getter
	// where nothing outside this component writes, a mutable reference where
	// something does.
	// ------------------------------------------------------------------
	bool const& chatInputFocused() const { return chatInputFocused_; }
	int const& currentFps() const { return currentFps_; }
	float const& essentialSimCostAvgUs() const { return essentialSimCostAvgUs_; }
	float const& fireGlowTime() const { return fireGlowTime_; }
	bool& frameTimingEnabled() { return frameTimingEnabled_; }
	bool const& frameTimingEnabled() const { return frameTimingEnabled_; }
	FrameTimings& timingsAccum() { return frameTimingsAccum_; }
	FrameTimings const& timingsAccum() const { return frameTimingsAccum_; }
	int& timingsSampleCount() { return frameTimingsSampleCount_; }
	int const& timingsSampleCount() const { return frameTimingsSampleCount_; }
	FrameTimings& timings() { return frameTimings_; }
	FrameTimings const& timings() const { return frameTimings_; }
	bool& have3DTransforms() { return have3DTransforms_; }
	bool const& have3DTransforms() const { return have3DTransforms_; }
	irr::u32& lastFpsTime() { return lastFpsTime_; }
	irr::u32 const& lastFpsTime() const { return lastFpsTime_; }
	uint32_t& lastPolygonCount() { return lastPolygonCount_; }
	uint32_t const& lastPolygonCount() const { return lastPolygonCount_; }
	float& loadingProgress() { return loadingProgress_; }
	float const& loadingProgress() const { return loadingProgress_; }
	std::wstring& loadingText() { return loadingText_; }
	std::wstring const& loadingText() const { return loadingText_; }
	float const& renderCostAvgUs() const { return renderCostAvgUs_; }

	// T335b test seam (B054). The frustum debug wireframe lives INSIDE
	// renderDebugOverlays(), whose every statement is pinned by
	// tests/goldens/frame_render_order_g05.txt, so it cannot be factored into a
	// callable of its own; the pixel test drives the whole pass instead, with
	// every other overlay in it left at its default OFF. These three are inline
	// on purpose: a definition in renderer_frame_render.cpp would be parsed as a
	// FrameComponent body by test_frame_render_order.cpp and reported as an
	// orphaned pass. Production code toggles frustumDebugDraw_ from
	// renderer_frame_input.cpp and fills regionBoundingBoxes during zone load.
	bool& frustumDebugDraw() { return frustumDebugDraw_; }
	void seedFrustumDebugBoxForTest(size_t regionIdx, const irr::core::aabbox3df& eqBbox) {
		ctx_.regionBoundingBoxes[regionIdx] = eqBbox;
	}
	size_t frustumDebugBoxCount() const { return ctx_.regionBoundingBoxes.size(); }
	void renderDebugOverlaysForTest(float deltaTime) { renderDebugOverlays(deltaTime); }

	// B973 (GitLab #291, frame/renderer_frame_entity_build.cpp): the
	// post-load entity work of one frame -- prep queue/dispatch/distribution
	// every frame, then build units under EntityRenderer::buildScheduler()'s
	// slice (the first unit always, in every governor state). Called from
	// processFrame() after zone load and from
	// ZoneStreamComponent::processFrameProgressiveLoad() while a re-armed
	// progressive load runs without the loading screen. Returns true when a
	// unit ran. Not called under WILLEQ_ENTITY_BUILD_LEGACY=1.
	bool runEntityBuildSlice();

private:
	// B893: fall damage. The renderer is the only thing that can see a fall --
	// it integrates gravity (isJumping, set on Space) and snaps to ground on
	// landing. processFrameInput() straddles updatePlayerMovement() to catch
	// the airborne transition: wasAirborne_ flips on the rising edge (record
	// the takeoff Z) and off on the landing edge (push a FallDamageIntent for
	// the drop, which the game thread turns into OP_EnvDamage). A jump that
	// lands back at its takeoff deals 0, so a normal standing jump never fires.
	bool wasAirborne_ = false;
	float fallTakeoffZ_ = 0.0f;
	float lastFallTrackX_ = 0.0f;
	float lastFallTrackY_ = 0.0f;

	// B973: account the frame's build work (the [entity-build] window line,
	// and [entity-build-frame] per frame under WILLEQ_ENTITY_BUILD_TRACE=1).
	void finishEntityBuildFrame(float frameMs);
	int entityBuildTrace_ = -1;            // -1 = env not read yet
	EntityBuildBacklog lastBuildBacklog_{};

	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// Phase methods with no caller outside processFrame().
	void processFrameInput(float deltaTime);
	void processFrameVisibility();
	void processFrameSimulation(float deltaTime);
	bool processFrameRender(float deltaTime);  // Returns false for loading screen early-return
	void processInputDeltas(float deltaTime);

	// G05 (refactor/G05_process_frame_render.md): the render passes
	// processFrameRender() calls, in this order. Each is a verbatim slice of
	// the pre-G05 body INCLUDING its measureSection() bracket, so the
	// frameTimings_ categories are unchanged; the ORDER is load-bearing and is
	// pinned by tests/test_frame_render_order.cpp against
	// tests/goldens/frame_render_order_g05.txt. No local of the original body
	// crossed a cut, so there is no cross-pass state object: everything these
	// share is already a member or reached through ctx_. Nothing about the
	// render/game thread boundary changed -- all sixteen run on the render
	// thread, in the same order, exactly as the single body did.
	// Callers: processFrameRender() and nothing else.
	// T423 (board B362): added to the pass list as its FIRST entry -- the
	// scene-ambient recompute, consumed from the dirty mark every non-clock
	// writer of an ambient input now sets.
	// T448 moved the following paragraph here out of irrlicht_renderer.h,
	// where it annotated markSceneAmbientDirty().
	//
	// T423 (board B362): the scene ambient is a function of FOUR inputs -- the
	// day/night clock, the user ambient multiplier, the weather modifier and
	// the vision floor -- and until this it was recomposed only when the CLOCK
	// moved. eqemu sends OP_TimeOfDay at zone-in and on `#set time`, never on
	// a timer, so between two clock packets a weather change or a vision buff
	// reached the world's lighting at an arbitrary later moment or never at
	// all. T418's first live session measured a whole day block of forced rain
	// and got zero `updateTimeOfDay: weatherMod ...` lines out of the client.
	//
	// Every writer of a non-clock input now MARKS the ambient dirty
	// (IrrlichtRenderer::markSceneAmbientDirty) and the render thread consumes
	// the mark once per frame here, pinned in the G05 frame-order golden.
	// IrrlichtRenderer::updateTimeOfDay() itself is the one recompute, and it
	// CLEARS the flag -- so the clock packet path is unchanged and a
	// marked-then-clocked frame recomposes once, not twice.
	//
	// The flag starts TRUE: the first frame after a zone load composes the
	// ambient whether or not the server has sent a clock packet yet.
	void recomposeSceneAmbientIfDirty();
	void runSceneProfileIfScheduled();
	void beginSceneAndDrawWorld(float deltaTime);
	void collectScenePassTimings();
	void renderFootprints();
	void checkFrameBudgetsAndLogStats();
	void renderTargetIndicator();
	void renderParticleEffects();
	void renderFireGlowAndBoids(float deltaTime);
	void renderWeatherEffects();
	void renderDebugOverlays(float deltaTime);
	void renderEntityCastingBars();
	void renderLegacyGui();
	void renderStaticUI();
	void renderZoneLineOverlays();
	void renderFrameTail();
	void endSceneAndSwap();

	// L12 (Batch L close-out, L11's hand-off): the frame-phase stopwatch.
	// L04 put the body in this family's TU but left the declaration and
	// sectionStart_ on IrrlichtRenderer, and L11 was told to leave it there;
	// the measurement was single-family in both units (35 call sites, all in
	// renderer_frame{,_input,_render}.cpp; zero readers of sectionStart_
	// outside them). Both come here now, in one substitution pass -- the body
	// in frame/renderer_frame.cpp is unchanged apart from its qualifier,
	// because sectionStart_ keeps its spelling.
	int64_t measureSection();
	std::chrono::steady_clock::time_point sectionStart_;

	// Frame-loop state referenced by no TU outside this family.
	bool pendingUIClick_ = false;  // Click consumed in Phase 1, applied to UI in render phase
	bool runTier2_ = true;
	bool runTier3_ = true;
	static constexpr uint32_t kTier2Interval = 3;   // ~20Hz at 60fps
	static constexpr uint32_t kTier3Interval = 6;   // ~10Hz at 60fps
	float tier3DeltaAccum_ = 0.0f;  // Accumulated delta for Tier 3 simulation
	float windTime_ = 0.0f;  // Monotonic wind time for GPU wind shader (seconds)
	bool pendingScreenshot_ = false;  // Deferred to after render, before swap
	float hudAnimTimer_ = 0.0f;  // Timer for HUD animations (auto attack indicator)
	int polygonBudgetExceededFrames_ = 0;  // Throttle warnings
	int constrainedStatsLogCounter_ = 0;   // Periodic stats logging
	bool frustumDebugDraw_ = false;  // Draw region bboxes colored by frustum result

	// T357 (B057): the Tab arm of processChatInput()'s key drain, and the
	// closure it completes from (empty until Application installs one -- a
	// bare renderer in a test then treats Tab as a no-op, exactly as a run
	// with no command registries would).
	void applyCommandCompletion();
	CommandCompletionProvider commandCompletionProvider_;

	// L13: owned state, see the accessors above.
	bool chatInputFocused_ = false;
	// T421: true while processTradeskillSearchInput() holds the keyboard
	// gate, so the release is a state change and not a per-frame write.
	bool textEntryGateHeld_ = false;
	// B897: the same, for the Friends window's two Editboxes.
	bool friendsGateHeld_ = false;
	// B442b: the same, for the QuantityWnd's box.
	bool quantityGateHeld_ = false;
	// B442b (#151 row 31): a left PRESS on the world with an item held,
	// waiting for its release. The release fires the drop prompt only if
	// the pointer has not moved (a press that moves is a camera drag: rows
	// G39/G40 sent nothing). pendingUIRelease_ is the frame's release latch,
	// read in processFrameInput() beside pendingUIClick_.
	bool pendingUIRelease_ = false;
	bool worldDropArmed_ = false;
	int worldDropX_ = 0;
	int worldDropY_ = 0;
	int currentFps_ = 0;
	float essentialSimCostAvgUs_ = 3000.0f;  // EMA of essential sim cost (us), init ~3ms
	float fireGlowTime_ = 0.0f;  // Animation time accumulator
	int frameCount_ = 0;
	bool frameTimingEnabled_ = false;  // Enable with /frametiming command
	FrameTimings frameTimingsAccum_;  // Accumulated over multiple frames
	int frameTimingsSampleCount_ = 0;
	FrameTimings frameTimings_;
	bool have3DTransforms_ = false;
	irr::u32 lastFpsTime_ = 0;
	uint32_t lastPolygonCount_ = 0;  // DiagnosticsComponent reads it; the two
	float loadingProgress_ = 0.0f;  // Loading progress for zone transitions (0.0 - 1.0)
	std::wstring loadingText_ = L"Initializing...";  // Loading stage text
	float renderCostAvgUs_ = 15000.0f;  // EMA of render phase cost (us), init ~15ms
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_FRAME_FRAME_COMPONENT_H
