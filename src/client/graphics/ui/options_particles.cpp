/*
 * The Options window's Display > Particles box (B995, #315): the
 * ODP_ParticlesTabs sub-tab strip (Spell / Env / Player) and the knobs its
 * pages set. See include/client/graphics/environment/particle_options.h for
 * what each knob does and the flagged assumptions (N1-N3).
 *
 * Geometry: the box is options_layout.h's ODP_ParticlesTabs rect (190,174)
 * 190x170; its sub-tab strip is the box's top 22px (the same 22 the page
 * origin (190,196) is derived from) split into three equal tabs -- the same
 * flagged equal-width deviation the main tab strip makes.
 */

#include "client/graphics/ui/options_panel.h"

#include <string>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

namespace {

using OptionsLayout::Setting;
namespace Env = Environment;

const irr::video::SColor kFrame(255, 60, 60, 70);
const irr::video::SColor kTabActiveFill(90, 255, 255, 255);
const irr::video::SColor kTabActiveText(255, 255, 255, 0);    // TabTextActiveColor
const irr::video::SColor kTabInactiveText(255, 255, 255, 255);// TabTextColor
const irr::video::SColor kHover(60, 255, 255, 255);

const char* const kSubTabText[kParticleSubTabs] = {"Spell", "Env", "Player"};

int step(const OptionsLayout::OptionValues& v, Setting s) { return v[static_cast<int>(s)]; }

}  // namespace

irr::core::rect<irr::s32> particleSubTabRect(const UILayout& layout, int i, int tab) {
    if (tab < 0 || tab >= kParticleSubTabs) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const auto box = OptionsLayout::controlRect(layout, i);
    const irr::s32 w = box.getWidth();
    const irr::s32 x0 = box.UpperLeftCorner.X + (w * tab) / kParticleSubTabs;
    const irr::s32 x1 = box.UpperLeftCorner.X + (w * (tab + 1)) / kParticleSubTabs;
    return irr::core::rect<irr::s32>(x0, box.UpperLeftCorner.Y, x1,
                                     box.UpperLeftCorner.Y + OptionsLayout::TAB_H);
}

int particleSubTabAt(const UILayout& layout, int i, int mouseX, int mouseY) {
    const irr::core::vector2di p(mouseX, mouseY);
    for (int t = 0; t < kParticleSubTabs; ++t) {
        if (particleSubTabRect(layout, i, t).isPointInside(p)) return t;
    }
    return -1;
}

void renderOptionsParticleTabs(UIRenderer& ui, const UILayout& layout,
                               const OptionsPanelState& state, int i) {
    auto* tb = ui.getTextBatch();
    ui.drawFrame(OptionsLayout::controlRect(layout, i), kFrame);
    for (int t = 0; t < kParticleSubTabs; ++t) {
        const auto r = particleSubTabRect(layout, i, t);
        if (t == state.particlesTab) ui.drawRect(r, kTabActiveFill);
        else if (state.hoveredControl == i) ui.drawRect(r, kHover);
        ui.drawFrame(r, kFrame);
        if (tb) {
            tb->addTextCentered(kSubTabText[t], r,
                                t == state.particlesTab ? kTabActiveText : kTabInactiveText);
        }
    }
}

Environment::ParticleKnobs particleKnobsFromOptions(const OptionsLayout::OptionValues& v) {
    Env::ParticleKnobs k = Env::identityParticleKnobs();
    auto& spell = k[static_cast<size_t>(Env::ParticleFamily::Spell)];
    spell.opacity = Env::opacityForStep(step(v, Setting::SpellParticleOpacity));
    spell.density = Env::densityForChoice(step(v, Setting::SpellParticleDensity));
    spell.nearClip = Env::nearClipForChoice(step(v, Setting::SpellParticleNearClip));
    auto& env = k[static_cast<size_t>(Env::ParticleFamily::Environment)];
    env.opacity = Env::opacityForStep(step(v, Setting::EnvParticleOpacity));
    env.density = Env::densityForChoice(step(v, Setting::EnvParticleDensity));
    env.nearClip = Env::nearClipForChoice(step(v, Setting::EnvParticleNearClip));
    // N3: Actor stays the identity -- fable has no actor particles.
    return k;
}

}  // namespace Graphics
}  // namespace EQT
