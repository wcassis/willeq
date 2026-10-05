// B975 (GitLab #293): EntityRenderer's one name-tag factory -- see
// include/client/graphics/name_tag_batch.h.

#include "client/graphics/entity_renderer.h"
#include "client/graphics/name_tag_batch.h"

namespace EQT {
namespace Graphics {

irr::scene::ITextSceneNode* EntityRenderer::createNameTagNode(irr::scene::ISceneNode* parent,
                                                              const std::wstring& text,
                                                              irr::video::SColor color,
                                                              const irr::core::vector3df& offset) {
    irr::gui::IGUIFont* font = smgr_->getGUIEnvironment()->getBuiltInFont();
    if (!font) return nullptr;  // addTextSceneNode()'s own rule
    if (!nameTagBatchEnabled()) {
        // The pre-B975 path, kept as the A/B switch: one CTextSceneNode each.
        return smgr_->addTextSceneNode(font, text.c_str(), color, parent, offset);
    }
    if (!nameTagBatch_) {
        nameTagBatch_ = new NameTagBatchNode(smgr_->getRootSceneNode(), smgr_, font);
        // The root holds one reference, this renderer the other.
    }
    auto* node = new NameTagNode(parent ? parent : smgr_->getRootSceneNode(), smgr_, nameTagBatch_,
                                 text.c_str(), color, offset);
    node->drop();  // the parent owns it, as addTextSceneNode() leaves it
    return node;
}

void EntityRenderer::releaseNameTagBatch() {
    if (!nameTagBatch_) return;
    nameTagBatch_->remove();
    nameTagBatch_->drop();
    nameTagBatch_ = nullptr;
}

} // namespace Graphics
} // namespace EQT
