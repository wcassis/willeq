// tools/model_viewer/model_viewer_input.cpp -- MyEventReceiver::OnEvent,
// the tool's whole keyboard map. The class declaration moved to
// model_viewer_state.h; the body is unchanged apart from being
// de-indented one level now that it is out of line.
// Bodies moved verbatim from tools/model_viewer.cpp by T385
// (board B167 -- that TU was 3,246 lines, over the 3,000-line hard cap).

#include "model_viewer_state.h"

bool MyEventReceiver::OnEvent(const SEvent& event) {
    if (event.EventType == EET_KEY_INPUT_EVENT && event.KeyInput.PressedDown) {
        switch (event.KeyInput.Key) {
            case KEY_ESCAPE:
                exit(0);
                return true;
            case KEY_F12:
                // Take screenshot
                if (driver) {
                    video::IImage* screenshot = driver->createScreenShot();
                    if (screenshot) {
                        driver->writeImageToFile(screenshot, "model_viewer.png");
                        screenshot->drop();
                        std::cout << "Screenshot saved to model_viewer.png" << std::endl;
                    }
                }
                return true;
            case KEY_LEFT:
                if (animatedNode) {
                    core::vector3df rot = animatedNode->getRotation();
                    rot.Y -= 10.0f;
                    animatedNode->setRotation(rot);
                } else if (modelNode) {
                    core::vector3df rot = modelNode->getRotation();
                    rot.Y -= 10.0f;
                    modelNode->setRotation(rot);
                }
                return true;
            case KEY_RIGHT:
                if (animatedNode) {
                    core::vector3df rot = animatedNode->getRotation();
                    rot.Y += 10.0f;
                    animatedNode->setRotation(rot);
                } else if (modelNode) {
                    core::vector3df rot = modelNode->getRotation();
                    rot.Y += 10.0f;
                    modelNode->setRotation(rot);
                }
                return true;
            case KEY_UP:
                if (animatedNode) {
                    core::vector3df rot = animatedNode->getRotation();
                    rot.X -= 10.0f;
                    animatedNode->setRotation(rot);
                } else if (modelNode) {
                    core::vector3df rot = modelNode->getRotation();
                    rot.X -= 10.0f;
                    modelNode->setRotation(rot);
                }
                return true;
            case KEY_DOWN:
                if (animatedNode) {
                    core::vector3df rot = animatedNode->getRotation();
                    rot.X += 10.0f;
                    animatedNode->setRotation(rot);
                } else if (modelNode) {
                    core::vector3df rot = modelNode->getRotation();
                    rot.X += 10.0f;
                    modelNode->setRotation(rot);
                }
                return true;
            case KEY_KEY_Q:
                if (animatedNode) {
                    core::vector3df rot = animatedNode->getRotation();
                    rot.Z -= 10.0f;
                    animatedNode->setRotation(rot);
                } else if (modelNode) {
                    core::vector3df rot = modelNode->getRotation();
                    rot.Z -= 10.0f;
                    modelNode->setRotation(rot);
                }
                return true;
            case KEY_KEY_E:
                if (animatedNode) {
                    core::vector3df rot = animatedNode->getRotation();
                    rot.Z += 10.0f;
                    animatedNode->setRotation(rot);
                } else if (modelNode) {
                    core::vector3df rot = modelNode->getRotation();
                    rot.Z += 10.0f;
                    modelNode->setRotation(rot);
                }
                return true;
            case KEY_KEY_R:
                if (animatedNode) {
                    animatedNode->setRotation(core::vector3df(0, 0, 0));
                } else if (modelNode) {
                    modelNode->setRotation(core::vector3df(0, 0, 0));
                }
                return true;
            case KEY_KEY_W:
                wireframe = !wireframe;
                if (animatedNode) {
                    for (u32 i = 0; i < animatedNode->getMaterialCount(); ++i) {
                        animatedNode->getMaterial(i).Wireframe = wireframe;
                    }
                }
                if (modelNode) {
                    for (u32 i = 0; i < modelNode->getMaterialCount(); ++i) {
                        modelNode->getMaterial(i).Wireframe = wireframe;
                    }
                }
                std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << std::endl;
                return true;
            case KEY_KEY_U:
                // Toggle U coordinate flip (texture X axis)
                flipU = !flipU;
                std::cout << "Flip U (texture X): " << (flipU ? "ON" : "OFF") << std::endl;
                if (useAnimatedMesh) {
                    reloadEntityWithTextures();
                } else {
                    rebuildMesh();
                }
                return true;
            case KEY_KEY_V:
                // Cycle body variant - V=next, Shift+V=prev
                if (!bodyVariants.empty()) {
                    if (event.KeyInput.Shift) {
                        currentBodyVariant = (currentBodyVariant + bodyVariants.size() - 1) % bodyVariants.size();
                    } else {
                        currentBodyVariant = (currentBodyVariant + 1) % bodyVariants.size();
                    }
                    std::cout << "Body variant: " << currentBodyVariant << "/" << bodyVariants.size()
                              << " (" << bodyVariants[currentBodyVariant]->name << ")" << std::endl;
                    if (useAnimatedMesh) {
                        reloadEntityWithTextures();
                    } else {
                        rebuildCurrentVariant();
                    }
                }
                return true;
            case KEY_KEY_A:
                // Cycle animations - A=next, Shift+A=prev
                if (event.KeyInput.Shift) {
                    cycleAnimation(-1);
                } else {
                    cycleAnimation(1);
                }
                return true;
            case KEY_SPACE:
                // Pause/Resume animation
                if (animatedNode && useAnimatedMesh) {
                    animationPaused = !animationPaused;
                    if (animationPaused) {
                        animatedNode->setAnimationSpeed(0.0f);
                        std::cout << "Animation PAUSED" << std::endl;
                    } else {
                        // setAnimationSpeed expects fps where 10 fps = 1.0x speed multiplier
                        animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
                        std::cout << "Animation RESUMED" << std::endl;
                    }
                }
                return true;
            case KEY_KEY_S:
                // Toggle static/animated mesh mode
                toggleAnimationMode();
                return true;
            case KEY_KEY_F:
                filterOutliers = !filterOutliers;
                std::cout << "Filter outliers: " << (filterOutliers ? "ON" : "OFF") << " - Rebuilding mesh..." << std::endl;
                rebuildMesh();
                return true;
            case KEY_PLUS:
            case KEY_ADD:
                // Zoom in
                cameraDistance = std::max(1.0f, cameraDistance - 2.0f);
                if (camera) {
                    camera->setPosition(core::vector3df(0, 5, -cameraDistance));
                }
                std::cout << "Zoom: " << cameraDistance << std::endl;
                return true;
            case KEY_MINUS:
            case KEY_SUBTRACT:
                // Zoom out
                cameraDistance = std::min(100.0f, cameraDistance + 2.0f);
                if (camera) {
                    camera->setPosition(core::vector3df(0, 5, -cameraDistance));
                }
                std::cout << "Zoom: " << cameraDistance << std::endl;
                return true;
            case KEY_OEM_4:  // '[' key - decrease animation speed
                globalAnimationSpeed = std::max(0.1f, globalAnimationSpeed - 0.1f);
                if (animatedNode && useAnimatedMesh) {
                    animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
                }
                std::cout << "Animation speed: " << globalAnimationSpeed << "x" << std::endl;
                return true;
            case KEY_OEM_6:  // ']' key - increase animation speed
                globalAnimationSpeed = std::min(5.0f, globalAnimationSpeed + 0.1f);
                if (animatedNode && useAnimatedMesh) {
                    animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
                }
                std::cout << "Animation speed: " << globalAnimationSpeed << "x" << std::endl;
                return true;
            case KEY_COMMA:  // '<' key - previous entity
                cycleEntity(-1);
                return true;
            case KEY_PERIOD:  // '>' key - next entity
                cycleEntity(1);
                return true;
            case KEY_KEY_B:  // B=toggle equipment textures on/off
                useEquipmentTextures = !useEquipmentTextures;
                std::cout << "Equipment textures: " << (useEquipmentTextures ? "ON" : "OFF");
                if (useEquipmentTextures) {
                    std::cout << " (texture=" << (int)currentBodyTexture << " " << getTextureName(currentBodyTexture) << ")";
                }
                std::cout << std::endl;
                if (useAnimatedMesh) {
                    reloadEntityWithTextures();
                } else {
                    rebuildCurrentVariant();
                }
                return true;
            case KEY_KEY_N:  // N=next body texture value, Shift+N=prev (when equipment textures enabled)
                if (useEquipmentTextures) {
                    auto it = std::find(validTextureValues.begin(), validTextureValues.end(), currentBodyTexture);
                    if (event.KeyInput.Shift) {
                        if (it != validTextureValues.begin() && it != validTextureValues.end()) {
                            --it;
                        } else {
                            it = validTextureValues.end() - 1;
                        }
                    } else {
                        if (it != validTextureValues.end()) {
                            ++it;
                            if (it == validTextureValues.end()) {
                                it = validTextureValues.begin();
                            }
                        } else {
                            it = validTextureValues.begin();
                        }
                    }
                    currentBodyTexture = *it;
                    std::cout << "Body texture: " << (int)currentBodyTexture << " (" << getTextureName(currentBodyTexture) << ")" << std::endl;
                    if (useAnimatedMesh) {
                        reloadEntityWithTextures();
                    } else {
                        rebuildCurrentVariant();
                    }
                } else {
                    std::cout << "Equipment textures are OFF. Press B to enable." << std::endl;
                }
                return true;
            case KEY_KEY_H:
                // H=next head variant, Shift+H=prev
                if (!headVariants.empty()) {
                    if (event.KeyInput.Shift) {
                        currentHeadVariant = (currentHeadVariant + headVariants.size() - 1) % headVariants.size();
                    } else {
                        currentHeadVariant = (currentHeadVariant + 1) % headVariants.size();
                    }
                    std::cout << "Head variant: " << currentHeadVariant << "/" << headVariants.size()
                              << " (" << headVariants[currentHeadVariant]->name << ")" << std::endl;
                    if (useAnimatedMesh) {
                        reloadEntityWithTextures();
                    } else {
                        rebuildCurrentVariant();
                    }
                }
                return true;
            case KEY_F3:
                // Spell test: cycle class
                if (spellTestMode) {
                    spellTestClassIndex += event.KeyInput.Shift ? -1 : 1;
                    if (spellTestClassIndex < 1) spellTestClassIndex = 16;
                    if (spellTestClassIndex > 16) spellTestClassIndex = 1;
                    rebuildSpellTestList();
                    return true;
                }
                break;
            case KEY_F4:
                // Spell test: cycle category
                if (spellTestMode) {
                    spellTestCategoryIndex += event.KeyInput.Shift ? -1 : 1;
                    int catCount = static_cast<int>(SpellTestCategory::Count);
                    if (spellTestCategoryIndex < 0) spellTestCategoryIndex = catCount - 1;
                    if (spellTestCategoryIndex >= catCount) spellTestCategoryIndex = 0;
                    rebuildSpellTestList();
                    return true;
                }
                break;
            case KEY_F5:
                // Spell test: cycle spell within category
                if (spellTestMode && !spellTestSpellList.empty()) {
                    spellTestSpellIndex += event.KeyInput.Shift ? -1 : 1;
                    if (spellTestSpellIndex < 0)
                        spellTestSpellIndex = static_cast<int>(spellTestSpellList.size()) - 1;
                    if (spellTestSpellIndex >= static_cast<int>(spellTestSpellList.size()))
                        spellTestSpellIndex = 0;
                    const auto* sp = spellTestSpellList[spellTestSpellIndex];
                    std::cout << "Spell [" << (spellTestSpellIndex + 1) << "/"
                              << spellTestSpellList.size() << "]: " << sp->name
                              << " (ID " << sp->id << ", " << getResistTypeName(sp->resist_type)
                              << ", cast " << sp->cast_time_ms << "ms)" << std::endl;
                    return true;
                }
                break;
            case KEY_F6:
                // Spell test: toggle self/targeted
                if (spellTestMode) {
                    spellTestSelfMode = !spellTestSelfMode;
                    std::cout << "Mode: " << (spellTestSelfMode ? "SELF" : "TARGETED") << std::endl;
                    return true;
                }
                break;
            case KEY_RETURN:
                // Spell test: cast current spell
                if (spellTestMode && !spellTestSpellList.empty() && !spellTestCasting) {
                    triggerSpellTestCast();
                    return true;
                }
                break;
            case KEY_BACK:
                // Spell test: cancel cast / clear effects
                if (spellTestMode) {
                    cancelSpellTestCast();
                    return true;
                }
                break;
            case KEY_F7:
                // F7=next effect, Shift+F7=prev effect (for testing EDD emitter definitions)
                if (event.KeyInput.Shift) {
                    cycleEffect(-1);
                } else {
                    cycleEffect(1);
                }
                return true;
            case KEY_F8:
                // F8: (reserved — cast effect style now auto-detected from spell school)
                break;
            default:
                // Check if it's a number key for spell casting (1-9, 0)
                if (event.KeyInput.Key >= KEY_KEY_0 && event.KeyInput.Key <= KEY_KEY_9) {
                    if (spellBar.handleKeyPress(event.KeyInput.Key)) {
                        return true;
                    }
                }
                break;
        }
    }

    // Handle mouse events for spell bar
    if (event.EventType == EET_MOUSE_INPUT_EVENT) {
        int mouseX = event.MouseInput.X;
        int mouseY = event.MouseInput.Y;

        switch (event.MouseInput.Event) {
            case EMIE_MOUSE_MOVED:
                spellBar.handleMouseMove(mouseX, mouseY);
                break;
            case EMIE_LMOUSE_PRESSED_DOWN:
                if (spellBar.handleMouseClick(mouseX, mouseY, true)) {
                    return true;
                }
                break;
            default:
                break;
        }
    }

return false;
}
