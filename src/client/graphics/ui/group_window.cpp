#include "client/graphics/ui/group_window.h"
#include "client/eq.h"
#include "client/combat.h"
#include "common/name_utils.h"

namespace eqt {
namespace ui {

GroupWindow::GroupWindow()
    : WindowBase(L"Group", 100, 100)  // Size calculated below
{
    // Initialize layout constants from UISettings
    const auto& groupSettings = UISettings::instance().group();
    NAME_HEIGHT = groupSettings.nameHeight;
    BAR_HEIGHT = groupSettings.barHeight;
    BAR_SPACING = groupSettings.barSpacing;
    MEMBER_SPACING = groupSettings.memberSpacing;
    BUTTON_WIDTH = groupSettings.buttonWidth;
    GROUP_BUTTON_HEIGHT = groupSettings.buttonHeight;
    WINDOW_PADDING = groupSettings.padding;
    GROUP_BUTTON_PADDING = groupSettings.buttonPadding;
    MEMBER_HEIGHT = NAME_HEIGHT + BAR_SPACING + BAR_HEIGHT + BAR_SPACING + BAR_HEIGHT;

    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();

    // Calculate window size
    int width = (BUTTON_WIDTH * 2 + GROUP_BUTTON_PADDING) + (borderWidth + contentPadding) * 2;
    int height = (WINDOW_PADDING + MAX_MEMBERS * (MEMBER_HEIGHT + MEMBER_SPACING) - MEMBER_SPACING +
                 GROUP_BUTTON_PADDING + GROUP_BUTTON_HEIGHT + GROUP_BUTTON_PADDING) + (borderWidth + contentPadding) * 2;
    setSize(width, height);

    setShowTitleBar(false);
    initializeLayout();
}

void GroupWindow::initializeLayout()
{
    // Content width matches constructor: 2 buttons + gap between them
    int contentWidth = BUTTON_WIDTH * 2 + GROUP_BUTTON_PADDING;

    // Member displays span full content width
    int memberBarWidth = contentWidth;

    // Layout member slots (5 other members)
    for (int i = 0; i < MAX_MEMBERS; i++) {
        int slotY = WINDOW_PADDING + i * (MEMBER_HEIGHT + MEMBER_SPACING);

        memberSlots_[i].nameBounds = irr::core::recti(
            0, slotY,
            memberBarWidth, slotY + NAME_HEIGHT
        );

        memberSlots_[i].hpBarBounds = irr::core::recti(
            0, slotY + NAME_HEIGHT + BAR_SPACING,
            memberBarWidth, slotY + NAME_HEIGHT + BAR_SPACING + BAR_HEIGHT
        );

        memberSlots_[i].manaBarBounds = irr::core::recti(
            0, slotY + NAME_HEIGHT + BAR_SPACING * 2 + BAR_HEIGHT,
            memberBarWidth, slotY + NAME_HEIGHT + BAR_SPACING * 2 + BAR_HEIGHT * 2
        );
    }

    // Button area at bottom
    int membersEndY = WINDOW_PADDING + MAX_MEMBERS * (MEMBER_HEIGHT + MEMBER_SPACING) - MEMBER_SPACING;
    int buttonY = membersEndY + GROUP_BUTTON_PADDING;

    // Buttons: left button at start, right button after gap
    int buttonX1 = 0;
    int buttonX2 = BUTTON_WIDTH + GROUP_BUTTON_PADDING;

    inviteButtonBounds_ = irr::core::recti(
        buttonX1, buttonY,
        buttonX1 + BUTTON_WIDTH, buttonY + GROUP_BUTTON_HEIGHT
    );

    disbandButtonBounds_ = irr::core::recti(
        buttonX2, buttonY,
        buttonX2 + BUTTON_WIDTH, buttonY + GROUP_BUTTON_HEIGHT
    );

    // Accept/Decline buttons (same positions, shown for pending invite)
    acceptButtonBounds_ = inviteButtonBounds_;
    declineButtonBounds_ = disbandButtonBounds_;
}

void GroupWindow::positionDefault(int screenWidth, int screenHeight)
{
    // Position below buff window (buff is at y=100, ~100 height)
    int x = screenWidth - getWidth() - 10;
    int y = 210;  // Below buff window
    setPosition(x, y);
}

void GroupWindow::update()
{
    if (!eq_) return;

    // Clear all slots first
    for (auto& slot : memberSlots_) {
        slot.isEmpty = true;
        slot.name.clear();
    }

    if (!eq_->IsInGroup()) {
        return;
    }

    // Populate from EverQuest group state (skip self, show other 5 members)
    std::string myName = eq_->GetMyName();
    int slotIndex = 0;
    for (int i = 0; i < 6 && slotIndex < MAX_MEMBERS; i++) {
        const GroupMember* member = eq_->GetGroupMember(i);
        if (member && !member->name.empty() && member->name != myName) {
            auto& slot = memberSlots_[slotIndex];
            slot.isEmpty = false;
            slot.name = EQT::toDisplayNameW(member->name);
            slot.hpPercent = member->hp_percent;
            slot.manaPercent = member->mana_percent;
            slot.isLeader = member->is_leader;
            slot.inZone = member->in_zone;
            slotIndex++;
        }
    }
}

void GroupWindow::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!visible_) {
        return;
    }

    // Draw window base (title bar, background, border)
    WindowBase::render(driver, gui);
}

void GroupWindow::renderContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!driver) return;

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // If showing pending invite, draw that instead
    if (showingPendingInvite_) {
        drawPendingInvite(driver, gui);
        return;
    }

    // Draw member slots
    for (int i = 0; i < MAX_MEMBERS; i++) {
        drawMemberSlot(driver, gui, memberSlots_[i], i);
    }

    // Draw buttons
    bool canDisband = eq_ && eq_->IsInGroup();

    // Invite/Kick button logic:
    // - Default: "Invite" (disabled until player targeted)
    // - If group leader and target is a group member: "Kick"
    std::wstring inviteLabel = L"Invite";
    bool inviteEnabled = false;

    bool isLeader = eq_ && eq_->IsGroupLeader();
    bool targetIsGroupMember = isTargetGroupMember();
    bool targetIsPlayer = hasPlayerTarget();

    if (isLeader && targetIsGroupMember) {
        // Show Kick button when leader targets a group member
        inviteLabel = L"Kick";
        inviteEnabled = true;
    } else if (targetIsPlayer) {
        // Show Invite button when targeting a player (not in group)
        inviteLabel = L"Invite";
        // Can only invite if not in group, or if leader with room
        bool canInvite = !eq_->IsInGroup() || (isLeader && eq_->GetGroupMemberCount() < 6);
        inviteEnabled = canInvite && !targetIsGroupMember;
    }

    irr::core::recti inviteAbs(
        contentX + inviteButtonBounds_.UpperLeftCorner.X,
        contentY + inviteButtonBounds_.UpperLeftCorner.Y,
        contentX + inviteButtonBounds_.LowerRightCorner.X,
        contentY + inviteButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, inviteAbs, inviteLabel, inviteButtonHovered_, !inviteEnabled);

    // Disband/Leave button
    std::wstring disbandLabel = (eq_ && eq_->IsGroupLeader()) ? L"Disband" : L"Leave";

    irr::core::recti disbandAbs(
        contentX + disbandButtonBounds_.UpperLeftCorner.X,
        contentY + disbandButtonBounds_.UpperLeftCorner.Y,
        contentX + disbandButtonBounds_.LowerRightCorner.X,
        contentY + disbandButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, disbandAbs, disbandLabel, disbandButtonHovered_, !canDisband);
}

void GroupWindow::drawMemberSlot(irr::video::IVideoDriver* driver,
                                  irr::gui::IGUIEnvironment* gui,
                                  const GroupMemberSlot& slot, int index)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Calculate absolute bounds
    irr::core::recti nameBounds(
        contentX + slot.nameBounds.UpperLeftCorner.X,
        contentY + slot.nameBounds.UpperLeftCorner.Y,
        contentX + slot.nameBounds.LowerRightCorner.X,
        contentY + slot.nameBounds.LowerRightCorner.Y
    );

    irr::core::recti hpBounds(
        contentX + slot.hpBarBounds.UpperLeftCorner.X,
        contentY + slot.hpBarBounds.UpperLeftCorner.Y,
        contentX + slot.hpBarBounds.LowerRightCorner.X,
        contentY + slot.hpBarBounds.LowerRightCorner.Y
    );

    irr::core::recti manaBounds(
        contentX + slot.manaBarBounds.UpperLeftCorner.X,
        contentY + slot.manaBarBounds.UpperLeftCorner.Y,
        contentX + slot.manaBarBounds.LowerRightCorner.X,
        contentY + slot.manaBarBounds.LowerRightCorner.Y
    );

    if (slot.isEmpty) {
        // Draw empty slot placeholder (subtle background)
        irr::core::recti slotBg(
            nameBounds.UpperLeftCorner.X,
            nameBounds.UpperLeftCorner.Y,
            manaBounds.LowerRightCorner.X,
            manaBounds.LowerRightCorner.Y
        );
        driver->draw2DRectangle(getMemberBackground(), slotBg);
        return;
    }

    // Draw name (with leader indicator)
    if (gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            std::wstring displayName = slot.name;
            if (slot.isLeader) {
                displayName = L"* " + displayName;  // Leader marker
            }

            irr::video::SColor nameColor = slot.inZone ? getNameInZone() : getNameOutOfZone();

            // If leader, draw the asterisk in gold
            if (slot.isLeader && displayName.length() > 2) {
                // Draw asterisk in gold
                font->draw(L"*", nameBounds, getLeaderMarker());
                // Offset for the rest of the name
                irr::core::recti nameOnly(
                    nameBounds.UpperLeftCorner.X + 8,
                    nameBounds.UpperLeftCorner.Y,
                    nameBounds.LowerRightCorner.X,
                    nameBounds.LowerRightCorner.Y
                );
                font->draw(slot.name.c_str(), nameOnly, nameColor);
            } else {
                font->draw(displayName.c_str(), nameBounds, nameColor);
            }
        }
    }

    // Draw HP bar
    drawHealthBar(driver, hpBounds, slot.hpPercent);

    // Draw Mana bar
    drawManaBar(driver, manaBounds, slot.manaPercent);
}

void GroupWindow::drawHealthBar(irr::video::IVideoDriver* driver,
                                 const irr::core::recti& bounds, uint8_t percent)
{
    // Background
    driver->draw2DRectangle(getHpBackground(), bounds);

    // Fill based on percent
    if (percent > 0) {
        int fillWidth = (bounds.getWidth() * percent) / 100;
        irr::core::recti fillBounds(
            bounds.UpperLeftCorner.X, bounds.UpperLeftCorner.Y,
            bounds.UpperLeftCorner.X + fillWidth, bounds.LowerRightCorner.Y
        );

        // Color based on health level
        irr::video::SColor color;
        if (percent > 50) {
            color = getHpHigh();        // Green
        } else if (percent > 25) {
            color = getHpMedium();      // Yellow
        } else {
            color = getHpLow();         // Red
        }

        driver->draw2DRectangle(color, fillBounds);
    }

    // Border
    driver->draw2DRectangleOutline(bounds, irr::video::SColor(255, 60, 60, 60));
}

void GroupWindow::drawManaBar(irr::video::IVideoDriver* driver,
                               const irr::core::recti& bounds, uint8_t percent)
{
    // Background
    driver->draw2DRectangle(getManaBackground(), bounds);

    // Fill based on percent
    if (percent > 0) {
        int fillWidth = (bounds.getWidth() * percent) / 100;
        irr::core::recti fillBounds(
            bounds.UpperLeftCorner.X, bounds.UpperLeftCorner.Y,
            bounds.UpperLeftCorner.X + fillWidth, bounds.LowerRightCorner.Y
        );
        driver->draw2DRectangle(getManaFill(), fillBounds);
    }

    // Border
    driver->draw2DRectangleOutline(bounds, irr::video::SColor(255, 60, 60, 60));
}

void GroupWindow::drawPendingInvite(irr::video::IVideoDriver* driver,
                                     irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Draw invite message
    if (gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            std::wstring msg1 = L"Group invite from:";
            std::wstring msg2 = std::wstring(pendingInviterName_.begin(), pendingInviterName_.end());

            irr::core::recti msgBounds1(
                contentX + WINDOW_PADDING, contentY + WINDOW_PADDING + 20,
                contentX + getWidth() - WINDOW_PADDING, contentY + WINDOW_PADDING + 34
            );
            irr::core::recti msgBounds2(
                contentX + WINDOW_PADDING, contentY + WINDOW_PADDING + 40,
                contentX + getWidth() - WINDOW_PADDING, contentY + WINDOW_PADDING + 54
            );

            font->draw(msg1.c_str(), msgBounds1, irr::video::SColor(255, 200, 200, 200));
            font->draw(msg2.c_str(), msgBounds2, irr::video::SColor(255, 255, 255, 100));
        }
    }

    // Draw Accept/Decline buttons
    irr::core::recti acceptAbs(
        contentX + acceptButtonBounds_.UpperLeftCorner.X,
        contentY + acceptButtonBounds_.UpperLeftCorner.Y,
        contentX + acceptButtonBounds_.LowerRightCorner.X,
        contentY + acceptButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, acceptAbs, L"Accept", acceptButtonHovered_);

    irr::core::recti declineAbs(
        contentX + declineButtonBounds_.UpperLeftCorner.X,
        contentY + declineButtonBounds_.UpperLeftCorner.Y,
        contentX + declineButtonBounds_.LowerRightCorner.X,
        contentY + declineButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, declineAbs, L"Decline", declineButtonHovered_);
}

bool GroupWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!visible_ || !leftButton) {
        return WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl);
    }

    // Check if click is in content area
    irr::core::recti contentArea = getContentArea();
    irr::core::vector2di clickPos(x, y);

    if (showingPendingInvite_) {
        // Check Accept button
        irr::core::recti acceptAbs(
            contentArea.UpperLeftCorner.X + acceptButtonBounds_.UpperLeftCorner.X,
            contentArea.UpperLeftCorner.Y + acceptButtonBounds_.UpperLeftCorner.Y,
            contentArea.UpperLeftCorner.X + acceptButtonBounds_.LowerRightCorner.X,
            contentArea.UpperLeftCorner.Y + acceptButtonBounds_.LowerRightCorner.Y
        );
        if (acceptAbs.isPointInside(clickPos)) {
            if (acceptCallback_) acceptCallback_();
            return true;
        }

        // Check Decline button
        irr::core::recti declineAbs(
            contentArea.UpperLeftCorner.X + declineButtonBounds_.UpperLeftCorner.X,
            contentArea.UpperLeftCorner.Y + declineButtonBounds_.UpperLeftCorner.Y,
            contentArea.UpperLeftCorner.X + declineButtonBounds_.LowerRightCorner.X,
            contentArea.UpperLeftCorner.Y + declineButtonBounds_.LowerRightCorner.Y
        );
        if (declineAbs.isPointInside(clickPos)) {
            if (declineCallback_) declineCallback_();
            return true;
        }
    } else {
        // Check Invite/Kick button
        irr::core::recti inviteAbs(
            contentArea.UpperLeftCorner.X + inviteButtonBounds_.UpperLeftCorner.X,
            contentArea.UpperLeftCorner.Y + inviteButtonBounds_.UpperLeftCorner.Y,
            contentArea.UpperLeftCorner.X + inviteButtonBounds_.LowerRightCorner.X,
            contentArea.UpperLeftCorner.Y + inviteButtonBounds_.LowerRightCorner.Y
        );
        if (inviteAbs.isPointInside(clickPos)) {
            bool isLeader = eq_ && eq_->IsGroupLeader();
            bool targetIsGroupMember = isTargetGroupMember();
            bool targetIsPlayer = hasPlayerTarget();

            if (isLeader && targetIsGroupMember) {
                // Kick mode
                if (kickCallback_) {
                    kickCallback_(getTargetName());
                }
            } else if (targetIsPlayer) {
                // Invite mode (only if enabled)
                bool canInvite = !eq_->IsInGroup() || (isLeader && eq_->GetGroupMemberCount() < 6);
                if (canInvite && !targetIsGroupMember && inviteCallback_) {
                    inviteCallback_();
                }
            }
            return true;
        }

        // Check Disband button
        irr::core::recti disbandAbs(
            contentArea.UpperLeftCorner.X + disbandButtonBounds_.UpperLeftCorner.X,
            contentArea.UpperLeftCorner.Y + disbandButtonBounds_.UpperLeftCorner.Y,
            contentArea.UpperLeftCorner.X + disbandButtonBounds_.LowerRightCorner.X,
            contentArea.UpperLeftCorner.Y + disbandButtonBounds_.LowerRightCorner.Y
        );
        if (disbandAbs.isPointInside(clickPos)) {
            if (disbandCallback_ && eq_ && eq_->IsInGroup()) {
                disbandCallback_();
            }
            return true;
        }
    }

    return WindowBase::handleMouseDown(x, y, leftButton, shift);
}

bool GroupWindow::handleMouseMove(int x, int y)
{
    if (!visible_) {
        return WindowBase::handleMouseMove(x, y);
    }

    irr::core::recti contentArea = getContentArea();
    irr::core::vector2di pos(x, y);

    // Update button hover states
    irr::core::recti inviteAbs(
        contentArea.UpperLeftCorner.X + inviteButtonBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + inviteButtonBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + inviteButtonBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + inviteButtonBounds_.LowerRightCorner.Y
    );
    inviteButtonHovered_ = inviteAbs.isPointInside(pos);

    irr::core::recti disbandAbs(
        contentArea.UpperLeftCorner.X + disbandButtonBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + disbandButtonBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + disbandButtonBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + disbandButtonBounds_.LowerRightCorner.Y
    );
    disbandButtonHovered_ = disbandAbs.isPointInside(pos);

    irr::core::recti acceptAbs(
        contentArea.UpperLeftCorner.X + acceptButtonBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + acceptButtonBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + acceptButtonBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + acceptButtonBounds_.LowerRightCorner.Y
    );
    acceptButtonHovered_ = acceptAbs.isPointInside(pos);

    irr::core::recti declineAbs(
        contentArea.UpperLeftCorner.X + declineButtonBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + declineButtonBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + declineButtonBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + declineButtonBounds_.LowerRightCorner.Y
    );
    declineButtonHovered_ = declineAbs.isPointInside(pos);

    return WindowBase::handleMouseMove(x, y);
}

void GroupWindow::showPendingInvite(const std::string& inviterName)
{
    showingPendingInvite_ = true;
    pendingInviterName_ = inviterName;
}

void GroupWindow::hidePendingInvite()
{
    showingPendingInvite_ = false;
    pendingInviterName_.clear();
}

bool GroupWindow::hasPlayerTarget() const
{
    if (!eq_) return false;

    CombatManager* combat = eq_->GetCombatManager();
    if (!combat || !combat->HasTarget()) return false;

    uint16_t targetId = combat->GetTargetId();
    const auto& entities = eq_->GetEntities();
    auto it = entities.find(targetId);
    if (it == entities.end()) return false;

    // npc_type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
    return it->second.npc_type == 0;
}

bool GroupWindow::isTargetGroupMember() const
{
    if (!eq_ || !eq_->IsInGroup()) return false;

    std::string targetName = getTargetName();
    if (targetName.empty()) return false;

    // Check if target is in our group
    for (int i = 0; i < 6; i++) {
        const GroupMember* member = eq_->GetGroupMember(i);
        if (member && !member->name.empty() && member->name == targetName) {
            return true;
        }
    }
    return false;
}

std::string GroupWindow::getTargetName() const
{
    if (!eq_) return "";

    CombatManager* combat = eq_->GetCombatManager();
    if (!combat || !combat->HasTarget()) return "";

    uint16_t targetId = combat->GetTargetId();
    const auto& entities = eq_->GetEntities();
    auto it = entities.find(targetId);
    if (it == entities.end()) return "";

    return it->second.name;
}

} // namespace ui
} // namespace eqt
