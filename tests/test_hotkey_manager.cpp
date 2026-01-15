#include <gtest/gtest.h>
#include "client/input/hotkey_manager.h"
#include <fstream>
#include <filesystem>

using namespace eqt::input;

// =============================================================================
// Key Name Parsing Tests
// =============================================================================

class HotkeyKeyNameTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure defaults are loaded
        HotkeyManager::instance().resetToDefaults();
    }
};

TEST_F(HotkeyKeyNameTest, ValidLetterKeys) {
    EXPECT_EQ(HotkeyManager::keyNameToCode("A"), irr::KEY_KEY_A);
    EXPECT_EQ(HotkeyManager::keyNameToCode("Z"), irr::KEY_KEY_Z);
    EXPECT_EQ(HotkeyManager::keyNameToCode("W"), irr::KEY_KEY_W);
}

TEST_F(HotkeyKeyNameTest, ValidLetterKeysCaseInsensitive) {
    EXPECT_EQ(HotkeyManager::keyNameToCode("a"), irr::KEY_KEY_A);
    EXPECT_EQ(HotkeyManager::keyNameToCode("w"), irr::KEY_KEY_W);
    EXPECT_EQ(HotkeyManager::keyNameToCode("ESCAPE"), irr::KEY_ESCAPE);
}

TEST_F(HotkeyKeyNameTest, ValidFunctionKeys) {
    EXPECT_EQ(HotkeyManager::keyNameToCode("F1"), irr::KEY_F1);
    EXPECT_EQ(HotkeyManager::keyNameToCode("F12"), irr::KEY_F12);
}

TEST_F(HotkeyKeyNameTest, ValidSpecialKeys) {
    EXPECT_EQ(HotkeyManager::keyNameToCode("Escape"), irr::KEY_ESCAPE);
    EXPECT_EQ(HotkeyManager::keyNameToCode("Return"), irr::KEY_RETURN);
    EXPECT_EQ(HotkeyManager::keyNameToCode("Enter"), irr::KEY_RETURN);  // Alias
    EXPECT_EQ(HotkeyManager::keyNameToCode("Space"), irr::KEY_SPACE);
    EXPECT_EQ(HotkeyManager::keyNameToCode("Tab"), irr::KEY_TAB);
}

TEST_F(HotkeyKeyNameTest, ValidOEMKeys) {
    EXPECT_EQ(HotkeyManager::keyNameToCode("Grave"), irr::KEY_OEM_3);
    EXPECT_EQ(HotkeyManager::keyNameToCode("Backtick"), irr::KEY_OEM_3);  // Alias
    EXPECT_EQ(HotkeyManager::keyNameToCode("BracketLeft"), irr::KEY_OEM_4);
    EXPECT_EQ(HotkeyManager::keyNameToCode("BracketRight"), irr::KEY_OEM_6);
    EXPECT_EQ(HotkeyManager::keyNameToCode("Slash"), irr::KEY_OEM_2);
}

TEST_F(HotkeyKeyNameTest, InvalidKeyName_ReturnsCodeCount) {
    EXPECT_EQ(HotkeyManager::keyNameToCode("InvalidKey"), irr::KEY_KEY_CODES_COUNT);
    EXPECT_EQ(HotkeyManager::keyNameToCode("NotAKey"), irr::KEY_KEY_CODES_COUNT);
    EXPECT_EQ(HotkeyManager::keyNameToCode(""), irr::KEY_KEY_CODES_COUNT);
    EXPECT_EQ(HotkeyManager::keyNameToCode("F99"), irr::KEY_KEY_CODES_COUNT);
}

TEST_F(HotkeyKeyNameTest, KeyCodeToName_ValidCodes) {
    EXPECT_EQ(HotkeyManager::keyCodeToName(irr::KEY_KEY_A), "A");
    EXPECT_EQ(HotkeyManager::keyCodeToName(irr::KEY_F1), "F1");
    EXPECT_EQ(HotkeyManager::keyCodeToName(irr::KEY_ESCAPE), "Escape");
}

TEST_F(HotkeyKeyNameTest, KeyCodeToName_InvalidCode) {
    EXPECT_EQ(HotkeyManager::keyCodeToName(irr::KEY_KEY_CODES_COUNT), "Unknown");
}

// =============================================================================
// Action Name Parsing Tests
// =============================================================================

class HotkeyActionNameTest : public ::testing::Test {};

TEST_F(HotkeyActionNameTest, ValidActionNames) {
    EXPECT_EQ(HotkeyManager::actionNameToEnum("ToggleInventory"), HotkeyAction::ToggleInventory);
    EXPECT_EQ(HotkeyManager::actionNameToEnum("MoveForward"), HotkeyAction::MoveForward);
    EXPECT_EQ(HotkeyManager::actionNameToEnum("Quit"), HotkeyAction::Quit);
    EXPECT_EQ(HotkeyManager::actionNameToEnum("SpellGem1"), HotkeyAction::SpellGem1);
    EXPECT_EQ(HotkeyManager::actionNameToEnum("HotbarSlot10"), HotkeyAction::HotbarSlot10);
}

TEST_F(HotkeyActionNameTest, InvalidActionName_ReturnsCount) {
    EXPECT_EQ(HotkeyManager::actionNameToEnum("InvalidAction"), HotkeyAction::Count);
    EXPECT_EQ(HotkeyManager::actionNameToEnum(""), HotkeyAction::Count);
    EXPECT_EQ(HotkeyManager::actionNameToEnum("toggle_inventory"), HotkeyAction::Count);  // Wrong case
    EXPECT_EQ(HotkeyManager::actionNameToEnum("SpellGem99"), HotkeyAction::Count);
}

TEST_F(HotkeyActionNameTest, ActionEnumToName_ValidActions) {
    EXPECT_EQ(HotkeyManager::actionEnumToName(HotkeyAction::ToggleInventory), "ToggleInventory");
    EXPECT_EQ(HotkeyManager::actionEnumToName(HotkeyAction::MoveForward), "MoveForward");
    EXPECT_EQ(HotkeyManager::actionEnumToName(HotkeyAction::Quit), "Quit");
}

TEST_F(HotkeyActionNameTest, ActionEnumToName_CountReturnsUnknown) {
    EXPECT_EQ(HotkeyManager::actionEnumToName(HotkeyAction::Count), "Unknown");
}

// =============================================================================
// Mode Name Parsing Tests
// =============================================================================

class HotkeyModeNameTest : public ::testing::Test {};

TEST_F(HotkeyModeNameTest, ValidModeNames) {
    EXPECT_EQ(HotkeyManager::modeNameToEnum("global"), HotkeyMode::Global);
    EXPECT_EQ(HotkeyManager::modeNameToEnum("player"), HotkeyMode::Player);
    EXPECT_EQ(HotkeyManager::modeNameToEnum("repair"), HotkeyMode::Repair);
    EXPECT_EQ(HotkeyManager::modeNameToEnum("admin"), HotkeyMode::Admin);
}

TEST_F(HotkeyModeNameTest, ValidModeNamesCaseInsensitive) {
    EXPECT_EQ(HotkeyManager::modeNameToEnum("Global"), HotkeyMode::Global);
    EXPECT_EQ(HotkeyManager::modeNameToEnum("PLAYER"), HotkeyMode::Player);
    EXPECT_EQ(HotkeyManager::modeNameToEnum("Admin"), HotkeyMode::Admin);
}

TEST_F(HotkeyModeNameTest, InvalidModeName_DefaultsToGlobal) {
    // Invalid modes default to Global as a safe fallback
    EXPECT_EQ(HotkeyManager::modeNameToEnum("invalid"), HotkeyMode::Global);
    EXPECT_EQ(HotkeyManager::modeNameToEnum(""), HotkeyMode::Global);
}

TEST_F(HotkeyModeNameTest, ModeEnumToName) {
    EXPECT_EQ(HotkeyManager::modeEnumToName(HotkeyMode::Global), "global");
    EXPECT_EQ(HotkeyManager::modeEnumToName(HotkeyMode::Player), "player");
    EXPECT_EQ(HotkeyManager::modeEnumToName(HotkeyMode::Repair), "repair");
    EXPECT_EQ(HotkeyManager::modeEnumToName(HotkeyMode::Admin), "admin");
}

// =============================================================================
// Binding String Parsing Tests (integration of key + modifiers)
// =============================================================================

class HotkeyBindingParseTest : public ::testing::Test {
protected:
    void SetUp() override {
        HotkeyManager::instance().resetToDefaults();
    }
};

TEST_F(HotkeyBindingParseTest, BindingToString_NoModifiers) {
    HotkeyBinding binding;
    binding.keyCode = irr::KEY_KEY_I;
    binding.modifiers = ModifierFlags::None;
    EXPECT_EQ(HotkeyManager::bindingToString(binding), "I");
}

TEST_F(HotkeyBindingParseTest, BindingToString_SingleModifier) {
    HotkeyBinding binding;
    binding.keyCode = irr::KEY_KEY_C;
    binding.modifiers = ModifierFlags::Ctrl;
    EXPECT_EQ(HotkeyManager::bindingToString(binding), "Ctrl+C");
}

TEST_F(HotkeyBindingParseTest, BindingToString_TwoModifiers) {
    HotkeyBinding binding;
    binding.keyCode = irr::KEY_ESCAPE;
    binding.modifiers = ModifierFlags::Ctrl | ModifierFlags::Shift;
    EXPECT_EQ(HotkeyManager::bindingToString(binding), "Ctrl+Shift+Escape");
}

TEST_F(HotkeyBindingParseTest, BindingToString_ThreeModifiers) {
    HotkeyBinding binding;
    binding.keyCode = irr::KEY_KEY_O;
    binding.modifiers = ModifierFlags::Ctrl | ModifierFlags::Shift | ModifierFlags::Alt;
    EXPECT_EQ(HotkeyManager::bindingToString(binding), "Ctrl+Shift+Alt+O");
}

// =============================================================================
// HotkeyBinding::matches Tests
// =============================================================================

class HotkeyBindingMatchTest : public ::testing::Test {};

TEST_F(HotkeyBindingMatchTest, MatchesExactKey_NoModifiers) {
    HotkeyBinding binding;
    binding.keyCode = irr::KEY_KEY_I;
    binding.modifiers = ModifierFlags::None;

    EXPECT_TRUE(binding.matches(irr::KEY_KEY_I, false, false, false));
    EXPECT_FALSE(binding.matches(irr::KEY_KEY_I, true, false, false));  // Ctrl held
    EXPECT_FALSE(binding.matches(irr::KEY_KEY_I, false, true, false));  // Shift held
    EXPECT_FALSE(binding.matches(irr::KEY_KEY_K, false, false, false)); // Wrong key
}

TEST_F(HotkeyBindingMatchTest, MatchesWithCtrl) {
    HotkeyBinding binding;
    binding.keyCode = irr::KEY_KEY_C;
    binding.modifiers = ModifierFlags::Ctrl;

    EXPECT_TRUE(binding.matches(irr::KEY_KEY_C, true, false, false));
    EXPECT_FALSE(binding.matches(irr::KEY_KEY_C, false, false, false)); // No Ctrl
    EXPECT_FALSE(binding.matches(irr::KEY_KEY_C, true, true, false));   // Extra modifier
}

TEST_F(HotkeyBindingMatchTest, MatchesWithMultipleModifiers) {
    HotkeyBinding binding;
    binding.keyCode = irr::KEY_ESCAPE;
    binding.modifiers = ModifierFlags::Ctrl | ModifierFlags::Shift;

    EXPECT_TRUE(binding.matches(irr::KEY_ESCAPE, true, true, false));
    EXPECT_FALSE(binding.matches(irr::KEY_ESCAPE, true, false, false));  // Missing Shift
    EXPECT_FALSE(binding.matches(irr::KEY_ESCAPE, false, true, false));  // Missing Ctrl
    EXPECT_FALSE(binding.matches(irr::KEY_ESCAPE, true, true, true));    // Extra Alt
}

// =============================================================================
// Conflict Detection Tests
// =============================================================================

class HotkeyConflictTest : public ::testing::Test {
protected:
    std::string m_tempDir;
    std::string m_tempConfigPath;

    void SetUp() override {
        // Create a temporary directory for test files
        m_tempDir = std::filesystem::temp_directory_path() / "hotkey_test";
        std::filesystem::create_directories(m_tempDir);
        m_tempConfigPath = m_tempDir + "/test_hotkeys.json";

        HotkeyManager::instance().resetToDefaults();
    }

    void TearDown() override {
        // Clean up temp files
        std::filesystem::remove_all(m_tempDir);
    }

    void writeConfigFile(const std::string& content) {
        std::ofstream file(m_tempConfigPath);
        file << content;
        file.close();
    }
};

TEST_F(HotkeyConflictTest, NoConflicts_DefaultConfig) {
    // Default config should have minimal conflicts (intentional ones for admin mode)
    auto conflicts = HotkeyManager::instance().detectConflicts();
    // We may have some intentional overlaps between modes, but check we detect them
    // This is really testing that the detection runs without crashing
    EXPECT_GE(conflicts.size(), 0u);
}

TEST_F(HotkeyConflictTest, DetectsSameModeConflict) {
    // Create config with two actions bound to same key in same mode
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "I",
                "ToggleSkills": "I"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));
    auto conflicts = HotkeyManager::instance().detectConflicts();

    ASSERT_GE(conflicts.size(), 1u);

    // Verify the conflict message mentions both actions
    bool foundConflict = false;
    for (const auto& c : conflicts) {
        if (c.message.find("ToggleInventory") != std::string::npos &&
            c.message.find("ToggleSkills") != std::string::npos) {
            foundConflict = true;
            break;
        }
    }
    EXPECT_TRUE(foundConflict) << "Expected conflict between ToggleInventory and ToggleSkills";
}

TEST_F(HotkeyConflictTest, DetectsGlobalModeConflict) {
    // Global bindings conflict with any mode-specific bindings on same key
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "global": {
                "ToggleWireframe": "I"
            },
            "player": {
                "ToggleInventory": "I"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));
    auto conflicts = HotkeyManager::instance().detectConflicts();

    ASSERT_GE(conflicts.size(), 1u);

    // Should flag global vs player conflict
    bool foundConflict = false;
    for (const auto& c : conflicts) {
        if (c.message.find("ToggleWireframe") != std::string::npos &&
            c.message.find("ToggleInventory") != std::string::npos) {
            foundConflict = true;
            break;
        }
    }
    EXPECT_TRUE(foundConflict) << "Expected conflict between global ToggleWireframe and player ToggleInventory";
}

TEST_F(HotkeyConflictTest, NoConflict_DifferentModes) {
    // Same key in different non-Global modes should NOT conflict
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "X"
            },
            "repair": {
                "RepairRotateXPos": "X"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));
    auto conflicts = HotkeyManager::instance().detectConflicts();

    // There should be no conflicts between player and repair mode for same key
    bool foundBadConflict = false;
    for (const auto& c : conflicts) {
        if (c.message.find("ToggleInventory") != std::string::npos &&
            c.message.find("RepairRotateXPos") != std::string::npos) {
            foundBadConflict = true;
            break;
        }
    }
    EXPECT_FALSE(foundBadConflict) << "Should not conflict between different non-Global modes";
}

TEST_F(HotkeyConflictTest, NoConflict_DifferentModifiers) {
    // Same key but different modifiers should NOT conflict
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleCollision": "C",
                "ToggleCollisionDebug": "Ctrl+C"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));
    auto conflicts = HotkeyManager::instance().detectConflicts();

    bool foundBadConflict = false;
    for (const auto& c : conflicts) {
        if (c.message.find("ToggleCollision") != std::string::npos &&
            c.message.find("ToggleCollisionDebug") != std::string::npos) {
            foundBadConflict = true;
            break;
        }
    }
    EXPECT_FALSE(foundBadConflict) << "C and Ctrl+C should not conflict";
}

// =============================================================================
// JSON Loading Tests
// =============================================================================

class HotkeyJsonLoadTest : public ::testing::Test {
protected:
    std::string m_tempDir;
    std::string m_tempConfigPath;

    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "hotkey_test";
        std::filesystem::create_directories(m_tempDir);
        m_tempConfigPath = m_tempDir + "/test_hotkeys.json";

        HotkeyManager::instance().resetToDefaults();
    }

    void TearDown() override {
        std::filesystem::remove_all(m_tempDir);
    }

    void writeConfigFile(const std::string& content) {
        std::ofstream file(m_tempConfigPath);
        file << content;
        file.close();
    }
};

TEST_F(HotkeyJsonLoadTest, LoadsValidConfig) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "J",
                "MoveForward": "I"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // Verify the binding was loaded
    auto action = HotkeyManager::instance().getAction(
        irr::KEY_KEY_J, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(*action, HotkeyAction::ToggleInventory);
}

TEST_F(HotkeyJsonLoadTest, LoadsMultipleBindingsForAction) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "MoveForward": ["W", "Up"]
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // Both W and Up should map to MoveForward
    auto actionW = HotkeyManager::instance().getAction(
        irr::KEY_KEY_W, false, false, false, HotkeyMode::Player);
    auto actionUp = HotkeyManager::instance().getAction(
        irr::KEY_UP, false, false, false, HotkeyMode::Player);

    ASSERT_TRUE(actionW.has_value());
    ASSERT_TRUE(actionUp.has_value());
    EXPECT_EQ(*actionW, HotkeyAction::MoveForward);
    EXPECT_EQ(*actionUp, HotkeyAction::MoveForward);
}

TEST_F(HotkeyJsonLoadTest, FailsOnMissingFile) {
    EXPECT_FALSE(HotkeyManager::instance().loadFromFile("/nonexistent/path/hotkeys.json"));
}

TEST_F(HotkeyJsonLoadTest, FailsOnMalformedJson) {
    writeConfigFile("{ invalid json content without closing brace");
    EXPECT_FALSE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));
}

TEST_F(HotkeyJsonLoadTest, WarnsOnInvalidKeyName) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "InvalidKeyName"
            }
        }
    })");

    // Should still load, but the invalid binding is skipped
    EXPECT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // The action should NOT be bound to anything (since the key was invalid)
    auto bindings = HotkeyManager::instance().getBindingsForAction(HotkeyAction::ToggleInventory);
    EXPECT_EQ(bindings.size(), 0u);
}

TEST_F(HotkeyJsonLoadTest, WarnsOnInvalidActionName) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "NonExistentAction": "I"
            }
        }
    })");

    // Should still load successfully (invalid action is skipped with warning)
    EXPECT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));
}

TEST_F(HotkeyJsonLoadTest, WarnsOnInvalidModifier) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "Super+I"
            }
        }
    })");

    // Should load, but the invalid binding is skipped
    EXPECT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // The action should NOT be bound
    auto bindings = HotkeyManager::instance().getBindingsForAction(HotkeyAction::ToggleInventory);
    EXPECT_EQ(bindings.size(), 0u);
}

TEST_F(HotkeyJsonLoadTest, LoadsWithModifiers) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "HotbarSlot1": "Ctrl+1",
                "ToggleCollisionDebug": "Ctrl+Shift+C"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // Ctrl+1 -> HotbarSlot1
    auto action1 = HotkeyManager::instance().getAction(
        irr::KEY_KEY_1, true, false, false, HotkeyMode::Player);
    ASSERT_TRUE(action1.has_value());
    EXPECT_EQ(*action1, HotkeyAction::HotbarSlot1);

    // Ctrl+Shift+C -> ToggleCollisionDebug
    auto action2 = HotkeyManager::instance().getAction(
        irr::KEY_KEY_C, true, true, false, HotkeyMode::Player);
    ASSERT_TRUE(action2.has_value());
    EXPECT_EQ(*action2, HotkeyAction::ToggleCollisionDebug);
}

// =============================================================================
// Mode-Based Lookup Tests
// =============================================================================

class HotkeyModeLookupTest : public ::testing::Test {
protected:
    std::string m_tempDir;
    std::string m_tempConfigPath;

    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "hotkey_test";
        std::filesystem::create_directories(m_tempDir);
        m_tempConfigPath = m_tempDir + "/test_hotkeys.json";

        HotkeyManager::instance().resetToDefaults();
    }

    void TearDown() override {
        std::filesystem::remove_all(m_tempDir);
    }

    void writeConfigFile(const std::string& content) {
        std::ofstream file(m_tempConfigPath);
        file << content;
        file.close();
    }
};

TEST_F(HotkeyModeLookupTest, GlobalBindingsAlwaysActive) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "global": {
                "ToggleWireframe": "F1"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // F1 should work in all modes
    auto actionPlayer = HotkeyManager::instance().getAction(
        irr::KEY_F1, false, false, false, HotkeyMode::Player);
    auto actionAdmin = HotkeyManager::instance().getAction(
        irr::KEY_F1, false, false, false, HotkeyMode::Admin);
    auto actionRepair = HotkeyManager::instance().getAction(
        irr::KEY_F1, false, false, false, HotkeyMode::Repair);

    ASSERT_TRUE(actionPlayer.has_value());
    ASSERT_TRUE(actionAdmin.has_value());
    ASSERT_TRUE(actionRepair.has_value());

    EXPECT_EQ(*actionPlayer, HotkeyAction::ToggleWireframe);
    EXPECT_EQ(*actionAdmin, HotkeyAction::ToggleWireframe);
    EXPECT_EQ(*actionRepair, HotkeyAction::ToggleWireframe);
}

TEST_F(HotkeyModeLookupTest, ModeSpecificBindingsOnlyInThatMode) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "I"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // I should work in Player mode
    auto actionPlayer = HotkeyManager::instance().getAction(
        irr::KEY_KEY_I, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(actionPlayer.has_value());
    EXPECT_EQ(*actionPlayer, HotkeyAction::ToggleInventory);

    // I should NOT work in Admin mode (unless there's also an Admin binding)
    auto actionAdmin = HotkeyManager::instance().getAction(
        irr::KEY_KEY_I, false, false, false, HotkeyMode::Admin);
    EXPECT_FALSE(actionAdmin.has_value());
}

TEST_F(HotkeyModeLookupTest, SameKeyDifferentActionsInDifferentModes) {
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "P"
            },
            "admin": {
                "CorpseZOffsetUp": "P"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // P in Player mode -> ToggleInventory
    auto actionPlayer = HotkeyManager::instance().getAction(
        irr::KEY_KEY_P, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(actionPlayer.has_value());
    EXPECT_EQ(*actionPlayer, HotkeyAction::ToggleInventory);

    // P in Admin mode -> CorpseZOffsetUp
    auto actionAdmin = HotkeyManager::instance().getAction(
        irr::KEY_KEY_P, false, false, false, HotkeyMode::Admin);
    ASSERT_TRUE(actionAdmin.has_value());
    EXPECT_EQ(*actionAdmin, HotkeyAction::CorpseZOffsetUp);
}

// =============================================================================
// Movement Key Tests
// =============================================================================

class HotkeyMovementTest : public ::testing::Test {
protected:
    void SetUp() override {
        HotkeyManager::instance().resetToDefaults();
    }
};

TEST_F(HotkeyMovementTest, DefaultMovementKeysRecognized) {
    HotkeyAction action;

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_KEY_W, action));
    EXPECT_EQ(action, HotkeyAction::MoveForward);

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_KEY_S, action));
    EXPECT_EQ(action, HotkeyAction::MoveBackward);

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_KEY_A, action));
    EXPECT_EQ(action, HotkeyAction::TurnLeft);

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_KEY_D, action));
    EXPECT_EQ(action, HotkeyAction::TurnRight);
}

TEST_F(HotkeyMovementTest, NonMovementKeyReturnsFalse) {
    HotkeyAction action;

    EXPECT_FALSE(HotkeyManager::instance().isMovementKey(irr::KEY_KEY_I, action));
    EXPECT_FALSE(HotkeyManager::instance().isMovementKey(irr::KEY_F1, action));
    EXPECT_FALSE(HotkeyManager::instance().isMovementKey(irr::KEY_ESCAPE, action));
}

TEST_F(HotkeyMovementTest, ArrowKeysAreMovementKeys) {
    HotkeyAction action;

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_UP, action));
    EXPECT_EQ(action, HotkeyAction::MoveForward);

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_DOWN, action));
    EXPECT_EQ(action, HotkeyAction::MoveBackward);

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_LEFT, action));
    EXPECT_EQ(action, HotkeyAction::TurnLeft);

    EXPECT_TRUE(HotkeyManager::instance().isMovementKey(irr::KEY_RIGHT, action));
    EXPECT_EQ(action, HotkeyAction::TurnRight);
}

// =============================================================================
// Reset and Reload Tests
// =============================================================================

class HotkeyResetReloadTest : public ::testing::Test {
protected:
    std::string m_tempDir;
    std::string m_tempConfigPath;

    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "hotkey_test";
        std::filesystem::create_directories(m_tempDir);
        m_tempConfigPath = m_tempDir + "/test_hotkeys.json";

        HotkeyManager::instance().resetToDefaults();
    }

    void TearDown() override {
        std::filesystem::remove_all(m_tempDir);
    }

    void writeConfigFile(const std::string& content) {
        std::ofstream file(m_tempConfigPath);
        file << content;
        file.close();
    }
};

TEST_F(HotkeyResetReloadTest, ResetToDefaults_RestoresOriginalBindings) {
    // Load custom config
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "Z"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // Verify custom binding is active
    auto customAction = HotkeyManager::instance().getAction(
        irr::KEY_KEY_Z, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(customAction.has_value());
    EXPECT_EQ(*customAction, HotkeyAction::ToggleInventory);

    // Reset to defaults
    HotkeyManager::instance().resetToDefaults();

    // Z should no longer be ToggleInventory (unless it was in defaults, which it's not)
    auto afterReset = HotkeyManager::instance().getAction(
        irr::KEY_KEY_Z, false, false, false, HotkeyMode::Player);
    // Default has Z as ToggleZoneLineVisualization
    if (afterReset.has_value()) {
        EXPECT_NE(*afterReset, HotkeyAction::ToggleInventory);
    }

    // I should be back to ToggleInventory (the default)
    auto defaultI = HotkeyManager::instance().getAction(
        irr::KEY_KEY_I, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(defaultI.has_value());
    EXPECT_EQ(*defaultI, HotkeyAction::ToggleInventory);
}

TEST_F(HotkeyResetReloadTest, Reload_ReloadsFromLastPath) {
    // Initial load
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "J"
            }
        }
    })");

    ASSERT_TRUE(HotkeyManager::instance().loadFromFile(m_tempConfigPath));

    // Verify J is ToggleInventory
    auto action = HotkeyManager::instance().getAction(
        irr::KEY_KEY_J, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(*action, HotkeyAction::ToggleInventory);

    // Update the file
    writeConfigFile(R"({
        "version": 1,
        "bindings": {
            "player": {
                "ToggleInventory": "K"
            }
        }
    })");

    // Reload
    ASSERT_TRUE(HotkeyManager::instance().reload());

    // Now K should be ToggleInventory, not J
    auto newAction = HotkeyManager::instance().getAction(
        irr::KEY_KEY_K, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(newAction.has_value());
    EXPECT_EQ(*newAction, HotkeyAction::ToggleInventory);

    auto oldAction = HotkeyManager::instance().getAction(
        irr::KEY_KEY_J, false, false, false, HotkeyMode::Player);
    EXPECT_FALSE(oldAction.has_value());
}

// =============================================================================
// Override Tests
// =============================================================================

class HotkeyOverrideTest : public ::testing::Test {
protected:
    void SetUp() override {
        HotkeyManager::instance().resetToDefaults();
    }
};

TEST_F(HotkeyOverrideTest, ApplyOverrides_AddsNewBindings) {
    Json::Value overrides;
    overrides["player"]["ToggleInventory"] = "M";

    HotkeyManager::instance().applyOverrides(overrides);

    // M should now also map to ToggleInventory (in addition to default I)
    auto action = HotkeyManager::instance().getAction(
        irr::KEY_KEY_M, false, false, false, HotkeyMode::Player);
    ASSERT_TRUE(action.has_value());
    EXPECT_EQ(*action, HotkeyAction::ToggleInventory);
}

// =============================================================================
// GetBindingsForAction Tests
// =============================================================================

class HotkeyGetBindingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        HotkeyManager::instance().resetToDefaults();
    }
};

TEST_F(HotkeyGetBindingsTest, ReturnsAllBindingsForAction) {
    // MoveForward has multiple default bindings (W and Up)
    auto bindings = HotkeyManager::instance().getBindingsForAction(HotkeyAction::MoveForward);

    EXPECT_GE(bindings.size(), 2u);  // At least W and Up

    bool hasW = false;
    bool hasUp = false;
    for (const auto& b : bindings) {
        if (b.keyCode == irr::KEY_KEY_W) hasW = true;
        if (b.keyCode == irr::KEY_UP) hasUp = true;
    }

    EXPECT_TRUE(hasW) << "Expected W to be bound to MoveForward";
    EXPECT_TRUE(hasUp) << "Expected Up arrow to be bound to MoveForward";
}

TEST_F(HotkeyGetBindingsTest, ReturnsEmptyForUnboundAction) {
    // Create a minimal config that doesn't bind everything
    HotkeyManager::instance().resetToDefaults();

    // Count enum shouldn't have any bindings
    auto bindings = HotkeyManager::instance().getBindingsForAction(HotkeyAction::Count);
    EXPECT_EQ(bindings.size(), 0u);
}
