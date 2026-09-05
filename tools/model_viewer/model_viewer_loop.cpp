// tools/model_viewer/model_viewer_loop.cpp -- the render loop: animation
// advance, spell-test cast progress, particle update, and the draw pass.
// Bodies moved from tools/model_viewer.cpp by T385 (board B167): main()
// alone was 933 lines. Only the mechanical changes are made here --
// locals that crossed a new function boundary became ViewerArgs fields or
// out-parameters, and `return 1` on a fatal path became `return false`.

#include "model_viewer_state.h"

void runViewerLoop() {
    u32 lastTime = device->getTimer()->getTime();

    // Main loop
    while (device->run()) {
        // Update animation
        u32 currentTime = device->getTimer()->getTime();
        u32 deltaMs = currentTime - lastTime;
        lastTime = currentTime;

        if (animatedNode && useAnimatedMesh && !animationPaused) {
            animatedNode->OnAnimate(currentTime);
            // Update equipment positions to follow hand bones
            updateEquipmentPositions();
        }

        // Update spell test mode casting
        float deltaSeconds = deltaMs / 1000.0f;

        if (spellTestMode && spellTestCasting) {
            spellTestCastElapsed += deltaSeconds;
            if (spellTestCastElapsed >= spellTestCastDuration) {
                completeSpellTestCast();
            }
        }

        // Return to idle after one-shot cast animation completes
        if (spellTestMode && !spellTestCasting && animatedNode) {
            // isPlayingThrough() is true while a non-looping anim is playing;
            // once it finishes (returns false), switch back to idle loop
            if (animatedNode->isPlayingThrough()) {
                spellTestCastAnimPlaying = true;
            } else if (spellTestCastAnimPlaying) {
                animatedNode->playAnimation("p01", true, false);
                spellTestCastAnimPlaying = false;
            }
        }

        // Update SpellVisualFX
        if (spellVisualFX) {
            spellVisualFX->update(deltaSeconds);
        }

        // Animate target node in spell test mode
        if (spellTestMode && targetAnimatedNode) {
            targetAnimatedNode->OnAnimate(currentTime);
        }

        #ifdef EQT_HAS_GLES2
        if (particleManager) {
            glm::vec3 camPosGlm(0);
            if (camera) {
                auto cp = camera->getAbsolutePosition();
                camPosGlm = glm::vec3(cp.X, cp.Y, cp.Z);
            }
            particleManager->updateUnified(deltaSeconds, camPosGlm);
        }
        #endif

        // Update casting bar and check for cast completion
        if (castingBar.update(deltaSeconds)) {
            // Cast completed!
            const EQT::SpellBarEntry* spell = castingBar.getCurrentSpell();
            if (spell) {
                std::cout << "Cast complete: " << spell->name << std::endl;

                // Stop casting particle effect and create completion effect
                spellFX.stopCastingEffect();
                if (animatedNode && useAnimatedMesh) {
                    spellFX.createCompletionEffect(animatedNode, *spell);

                    // Stop the looping cast animation and return to idle
                    animatedNode->stopAnimation();
                    // Optionally play idle animation
                    animatedNode->playAnimation("p01", true, false);
                }
            }
            castingBar.completeCast();
        }

        // Update spell particle effects
        spellFX.update(deltaSeconds);

        driver->beginScene(true, true, video::SColor(255, 50, 50, 80));
        smgr->drawAll();

        #ifdef EQT_HAS_GLES2
        // Render unified particles (point sprites) after 3D scene, before 2D overlay
        if (particleManager) {
            irr::core::matrix4 viewMat = driver->getTransform(video::ETS_VIEW);
            irr::core::matrix4 projMat = driver->getTransform(video::ETS_PROJECTION);
            irr::core::vector3df camPos = camera ? camera->getAbsolutePosition()
            : irr::core::vector3df(0, 0, 0);
            float screenH = static_cast<float>(driver->getScreenSize().Height);
            particleManager->renderUnified(viewMat, projMat, camPos,
                999999.0f, 999999.0f, nullptr, screenH);
        }
        #endif

        // Draw info text
        gui::IGUIFont* font = device->getGUIEnvironment()->getBuiltInFont();
        if (font) {
            // Mode and animation info
            std::wstring modeInfo = L"[S] Mode: ";
            modeInfo += useAnimatedMesh ? L"ANIMATED" : L"STATIC";
            if (useAnimatedMesh) {
                modeInfo += L"  [A/Z] Anim: ";
                if (currentAnimIndex < 0) {
                    modeInfo += L"NONE (pose)";
                } else if (currentAnimIndex < static_cast<int>(availableAnimations.size())) {
                    std::string animName = availableAnimations[currentAnimIndex];
                    modeInfo += std::wstring(animName.begin(), animName.end());
                    modeInfo += L" (" + std::to_wstring(currentAnimIndex + 1) + L"/" +
                    std::to_wstring(availableAnimations.size()) + L")";
                }
                modeInfo += animationPaused ? L" [PAUSED]" : L"";
                // Add speed info
                modeInfo += L"  [/] Speed: ";
                // Format speed with 1 decimal place
                int speedInt = static_cast<int>(globalAnimationSpeed * 10);
                modeInfo += std::to_wstring(speedInt / 10) + L"." + std::to_wstring(speedInt % 10) + L"x";
            }
            font->draw(modeInfo.c_str(), core::rect<s32>(10, 10, 700, 30), video::SColor(255, 255, 255, 255));

            // Variant info line
            {
                std::wstring variantInfo = L"[V] Body: " + std::to_wstring(currentBodyVariant + 1) + L"/" + std::to_wstring(bodyVariants.size());
                variantInfo += L"  [H] Head: " + std::to_wstring(currentHeadVariant + 1) + L"/" + std::to_wstring(headVariants.size());
                font->draw(variantInfo.c_str(), core::rect<s32>(10, 30, 600, 50), video::SColor(255, 255, 255, 255));
            }

            // Equipment texture info line
            {
                std::wstring texInfo = L"[B] Equip: ";
                texInfo += useEquipmentTextures ? L"ON" : L"OFF";
                if (useEquipmentTextures) {
                    texInfo += L"  [N] Tex: " + std::to_wstring(currentBodyTexture);
                }
                font->draw(texInfo.c_str(), core::rect<s32>(10, 50, 700, 70), video::SColor(255, 200, 255, 200));
            }

            // Wireframe and rotation info
            std::wstring wireInfo = L"[W] Wire: ";
            wireInfo += wireframe ? L"ON" : L"OFF";

            core::vector3df rot;
            if (animatedNode) {
                rot = animatedNode->getRotation();
            } else if (modelNode) {
                rot = modelNode->getRotation();
            }
            wireInfo += L"  Rot: X=" + std::to_wstring((int)rot.X)
            + L" Y=" + std::to_wstring((int)rot.Y)
            + L" Z=" + std::to_wstring((int)rot.Z);
            font->draw(wireInfo.c_str(), core::rect<s32>(10, 70, 500, 90),
                video::SColor(255, 255, 255, 255));

            // Model info
            std::wstring modelInfo = L"Model: ";
            modelInfo += std::wstring(currentRaceCode.begin(), currentRaceCode.end());
            modelInfo += L" (race " + std::to_wstring(currentRaceId) + L")";
            font->draw(modelInfo.c_str(), core::rect<s32>(10, 90, 400, 110),
                video::SColor(255, 200, 200, 200));

            // Spell bar controls info (only when not in spell test mode)
            if (!spellTestMode) {
                font->draw(L"[1-0] Cast spell  [Click gems on left]", core::rect<s32>(10, 110, 400, 130),
                    video::SColor(255, 180, 180, 255));
            }

            // Spell test mode HUD
            if (spellTestMode) {
                int hudY = 130;
                const int lineHeight = 14;
                video::SColor headerColor(255, 255, 200, 100);
                video::SColor valueColor(255, 255, 255, 255);
                video::SColor dimColor(255, 150, 150, 150);
                video::SColor castingColor(255, 100, 255, 100);

                // Header
                font->draw(L"=== SPELL TEST MODE ===", core::rect<s32>(10, hudY, 400, hudY + lineHeight), headerColor);
                hudY += lineHeight;

                // Class
                std::string classStr = "[F3] Class: " + std::string(getClassName(spellTestClassIndex));
                std::wstring wClassStr(classStr.begin(), classStr.end());
                font->draw(wClassStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), valueColor);
                hudY += lineHeight;

                // Category
                std::string catStr = "[F4] Category: " + std::string(spellTestCategoryNames[spellTestCategoryIndex]);
                std::wstring wCatStr(catStr.begin(), catStr.end());
                font->draw(wCatStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), valueColor);
                hudY += lineHeight;

                // Current spell
                if (!spellTestSpellList.empty() && spellTestSpellIndex < static_cast<int>(spellTestSpellList.size())) {
                    const auto* sp = spellTestSpellList[spellTestSpellIndex];
                    std::string spellStr = "[F5] Spell: " + sp->name + " (" +
                    std::to_string(spellTestSpellIndex + 1) + "/" +
                    std::to_string(spellTestSpellList.size()) + ")";
                    std::wstring wSpellStr(spellStr.begin(), spellStr.end());
                    font->draw(wSpellStr.c_str(), core::rect<s32>(10, hudY, 600, hudY + lineHeight), valueColor);
                    hudY += lineHeight;

                    // Spell details
                    std::string detailStr = "  ID:" + std::to_string(sp->id) +
                    " Resist:" + getResistTypeName(sp->resist_type) +
                    " Cast:" + std::to_string(sp->cast_time_ms / 1000) + "." +
                    std::to_string((sp->cast_time_ms / 100) % 10) + "s" +
                    " Mana:" + std::to_string(sp->mana_cost);
                    std::wstring wDetailStr(detailStr.begin(), detailStr.end());
                    font->draw(wDetailStr.c_str(), core::rect<s32>(10, hudY, 600, hudY + lineHeight), dimColor);
                    hudY += lineHeight;

                    // Target type
                    std::string targetStr = "  Target:" + std::string(
                        sp->isSelfOnly() ? "Self" :
                        sp->isPBAE() ? "PBAE" :
                        sp->isAESpell() ? "AE" :
                        sp->isGroupSpell() ? "Group" : "Single");
                    if (sp->is_beneficial) targetStr += " (Beneficial)";
                    std::wstring wTargetStr(targetStr.begin(), targetStr.end());
                    font->draw(wTargetStr.c_str(), core::rect<s32>(10, hudY, 600, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                } else {
                    font->draw(L"[F5] Spell: (none available)", core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                }

                // Mode
                std::string modeStr = "[F6] Mode: " + std::string(spellTestSelfMode ? "SELF" : "TARGETED");
                std::wstring wModeStr(modeStr.begin(), modeStr.end());
                font->draw(wModeStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), valueColor);
                hudY += lineHeight;

                // Cast effect style (auto-detected from spell school)
                {
                    std::string styleLabel = "SMOLDER";
                    std::string schoolLabel = "?";
                    if (!spellTestSpellList.empty() && spellTestSpellIndex < static_cast<int>(spellTestSpellList.size()) && spellVisualFX) {
                        const auto* curSpell = spellTestSpellList[spellTestSpellIndex];
                        // T381: the highlighted spell may not have been cast yet.
                        spellVisualFX->noteSpellVocabulary(curSpell->id, curSpell->visualVocabulary());
                        bool spray = spellVisualFX->isSprayCastStyle(curSpell->id);
                        styleLabel = spray ? "SPRAY" : "SMOLDER";
                        switch (curSpell->getSchool()) {
                            case EQ::SpellSchool::Abjuration:  schoolLabel = "Abjuration"; break;
                            case EQ::SpellSchool::Alteration:  schoolLabel = "Alteration"; break;
                            case EQ::SpellSchool::Conjuration: schoolLabel = "Conjuration"; break;
                            case EQ::SpellSchool::Divination:  schoolLabel = "Divination"; break;
                            case EQ::SpellSchool::Evocation:   schoolLabel = "Evocation"; break;
                        }
                    }
                    std::string effectStr = "Cast: " + styleLabel + " (" + schoolLabel + ")";
                    std::wstring wEffectStr(effectStr.begin(), effectStr.end());
                    font->draw(wEffectStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                }

                // Cast state
                if (spellTestCasting && spellTestCurrentSpell) {
                    float progress = spellTestCastElapsed / spellTestCastDuration;
                    int pct = static_cast<int>(progress * 100);
                    std::string castStr = "CASTING: " + spellTestCurrentSpell->name + " " +
                    std::to_string(pct) + "%";
                    std::wstring wCastStr(castStr.begin(), castStr.end());
                    font->draw(wCastStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), castingColor);
                    hudY += lineHeight;
                }

                // Active effects count
                if (spellVisualFX) {
                    std::string fxStr = "Active effects: " +
                    std::to_string(spellVisualFX->getActiveEffectCount());
                    std::wstring wFxStr(fxStr.begin(), fxStr.end());
                    font->draw(wFxStr.c_str(), core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
                    hudY += lineHeight;
                }

                // Controls reminder
                hudY += lineHeight;
                font->draw(L"[Enter] Cast  [Backspace] Cancel/Clear", core::rect<s32>(10, hudY, 400, hudY + lineHeight), dimColor);
            }
        }

        // Render spell bar UI
        spellBar.render();

        // Render casting bar UI
        castingBar.render();

        driver->endScene();
    }
}
