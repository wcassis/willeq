#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>

// Forward declaration
class EverQuest;

namespace eqt {
namespace ui {

// Callback types
using GroupInviteCallback = std::function<void()>;
using GroupKickCallback = std::function<void(const std::string& memberName)>;
using GroupDisbandCallback = std::function<void()>;
using GroupAcceptCallback = std::function<void()>;
using GroupDeclineCallback = std::function<void()>;

// Group member display slot
struct GroupMemberSlot {
    irr::core::recti nameBounds;
    irr::core::recti hpBarBounds;
    irr::core::recti manaBarBounds;
    std::wstring name;
    uint8_t hpPercent = 100;
    uint8_t manaPercent = 100;
    bool isLeader = false;
    bool inZone = false;
    bool isEmpty = true;
};

class GroupWindow : public WindowBase {
public:
    GroupWindow();
    ~GroupWindow() = default;

    // Set EverQuest reference for group data
    void setEQ(EverQuest* eq) { eq_ = eq; }

    // Position below buff window
    void positionDefault(int screenWidth, int screenHeight);

    // Update from group state (call each frame)
    void update();

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setInviteCallback(GroupInviteCallback cb) { inviteCallback_ = std::move(cb); }
    void setKickCallback(GroupKickCallback cb) { kickCallback_ = std::move(cb); }
    void setDisbandCallback(GroupDisbandCallback cb) { disbandCallback_ = std::move(cb); }
    void setAcceptCallback(GroupAcceptCallback cb) { acceptCallback_ = std::move(cb); }
    void setDeclineCallback(GroupDeclineCallback cb) { declineCallback_ = std::move(cb); }

    // Pending invite display
    void showPendingInvite(const std::string& inviterName);
    void hidePendingInvite();
    bool isShowingPendingInvite() const { return showingPendingInvite_; }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeLayout();
    void drawMemberSlot(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui,
                       const GroupMemberSlot& slot, int index);
    void drawHealthBar(irr::video::IVideoDriver* driver,
                      const irr::core::recti& bounds, uint8_t percent);
    void drawManaBar(irr::video::IVideoDriver* driver,
                    const irr::core::recti& bounds, uint8_t percent);
    void drawPendingInvite(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui);

    // Layout constants - initialized from UISettings
    int NAME_HEIGHT;
    int BAR_HEIGHT;
    int BAR_SPACING;
    int MEMBER_HEIGHT;
    int MEMBER_SPACING;
    int BUTTON_WIDTH;
    int GROUP_BUTTON_HEIGHT;
    static constexpr int MAX_MEMBERS = 5;         // 5 other members (self not shown)
    int WINDOW_PADDING;
    int GROUP_BUTTON_PADDING;

    // Color accessors - read from UISettings
    irr::video::SColor getHpBackground() const { return UISettings::instance().group().hpBackground; }
    irr::video::SColor getHpHigh() const { return UISettings::instance().group().hpHigh; }
    irr::video::SColor getHpMedium() const { return UISettings::instance().group().hpMedium; }
    irr::video::SColor getHpLow() const { return UISettings::instance().group().hpLow; }
    irr::video::SColor getManaBackground() const { return UISettings::instance().group().manaBackground; }
    irr::video::SColor getManaFill() const { return UISettings::instance().group().manaFill; }
    irr::video::SColor getMemberBackground() const { return UISettings::instance().group().memberBackground; }
    irr::video::SColor getNameInZone() const { return UISettings::instance().group().nameInZone; }
    irr::video::SColor getNameOutOfZone() const { return UISettings::instance().group().nameOutOfZone; }
    irr::video::SColor getLeaderMarker() const { return UISettings::instance().group().leaderMarker; }

    // Member slots
    std::array<GroupMemberSlot, MAX_MEMBERS> memberSlots_;

    // Buttons
    irr::core::recti inviteButtonBounds_;
    irr::core::recti disbandButtonBounds_;
    irr::core::recti acceptButtonBounds_;   // For pending invite
    irr::core::recti declineButtonBounds_;  // For pending invite
    bool inviteButtonHovered_ = false;
    bool disbandButtonHovered_ = false;
    bool acceptButtonHovered_ = false;
    bool declineButtonHovered_ = false;

    // State
    EverQuest* eq_ = nullptr;
    bool showingPendingInvite_ = false;
    std::string pendingInviterName_;

    // Button state helpers
    bool hasPlayerTarget() const;
    bool isTargetGroupMember() const;
    std::string getTargetName() const;

    // Callbacks
    GroupInviteCallback inviteCallback_;
    GroupKickCallback kickCallback_;
    GroupDisbandCallback disbandCallback_;
    GroupAcceptCallback acceptCallback_;
    GroupDeclineCallback declineCallback_;
};

} // namespace ui
} // namespace eqt
