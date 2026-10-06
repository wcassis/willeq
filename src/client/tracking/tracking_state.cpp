// B895 (GitLab #211): the Tracking skill's client-side rules. See the banner in
// include/client/tracking/tracking_state.h for where every rule comes from.

#include "client/tracking/tracking_state.h"

#include "client/display_eqstr.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace eqt {
namespace tracking {

namespace {

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Trim surrounding blanks: "/tracksort  distance " is still "distance".
    const size_t a = out.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    const size_t b = out.find_last_not_of(" \t");
    return out.substr(a, b - a + 1);
}

int ConRank(ConTier t) { return static_cast<int>(t); }  // Gray .. Red ascending

}  // namespace

uint32_t PlayersConfirmStringId(TrackPlayers p) {
    switch (p) {
        case TrackPlayers::On:    return 13140;
        case TrackPlayers::Off:   return 13141;
        case TrackPlayers::Group: return 13142;
    }
    return 0;
}

uint32_t SortConfirmStringId(TrackSort s) {
    return 13149u + static_cast<uint32_t>(s);  // normal..rconsider = 13149..13153
}

uint32_t FilterWordStringId(TrackFilter f) {
    switch (f) {
        case TrackFilter::Green:     return 13156;
        case TrackFilter::Blue:      return 13157;
        case TrackFilter::White:     return 13158;
        case TrackFilter::Red:       return 13159;
        case TrackFilter::Yellow:    return 13160;
        case TrackFilter::LightBlue: return 13161;
    }
    return 0;
}

uint32_t SortOptionStringId(TrackSort s) {
    switch (s) {
        case TrackSort::Normal:    return 13120;  // "normal"
        case TrackSort::Distance:  return 13144;
        case TrackSort::Consider:  return 13145;
        case TrackSort::RDistance: return 13146;
        case TrackSort::RConsider: return 13147;
    }
    return 0;
}

uint32_t PlayersOptionStringId(TrackPlayers p) {
    switch (p) {
        case TrackPlayers::On:    return 12947;  // "on"
        case TrackPlayers::Off:   return 12946;  // "off"
        case TrackPlayers::Group: return 13148;  // "Group"
    }
    return 0;
}

const std::vector<uint32_t>& TrackingEqStrIds() {
    static const std::vector<uint32_t> ids = [] {
        std::vector<uint32_t> v = {
            kStrBeginTracking, kStrNoTrackingTarget, kStrRight, kStrLeft,
            kStrStraightAhead, kStrAheadAndTo, kStrToThe, kStrBehindAndTo,
            kStrBehindYou, kStrLostTarget, kStrPlayersUsage, kStrSortUsage,
            kStrFilterUsage, kStrWillSee, kStrWillNotSee,
        };
        for (int i = 0; i < kPlayersCount; ++i) {
            v.push_back(PlayersConfirmStringId(static_cast<TrackPlayers>(i)));
            v.push_back(PlayersOptionStringId(static_cast<TrackPlayers>(i)));
        }
        for (int i = 0; i < kSortCount; ++i) {
            v.push_back(SortConfirmStringId(static_cast<TrackSort>(i)));
            v.push_back(SortOptionStringId(static_cast<TrackSort>(i)));
        }
        for (int i = 0; i < kFilterCount; ++i) {
            v.push_back(FilterWordStringId(static_cast<TrackFilter>(i)));
        }
        return v;
    }();
    return ids;
}

TrackFilter FilterForConTier(ConTier tier) {
    switch (tier) {
        case ConTier::Red:       return TrackFilter::Red;
        case ConTier::Yellow:    return TrackFilter::Yellow;
        case ConTier::White:     return TrackFilter::White;
        case ConTier::LightBlue: return TrackFilter::LightBlue;
        case ConTier::Green:     return TrackFilter::Green;
        case ConTier::Gray:      return TrackFilter::Green;  // assumption: no gray button
    }
    return TrackFilter::White;
}

std::vector<TrackEntry> VisibleRows(const std::vector<TrackEntry>& entries,
                                    const TrackingSettings& settings) {
    std::vector<TrackEntry> rows;
    rows.reserve(entries.size());
    for (const auto& e : entries) {
        if (e.isPlayer) {
            if (settings.players == TrackPlayers::Off) continue;
            if (settings.players == TrackPlayers::Group && !e.isGroupMember) continue;
        } else {
            const auto f = static_cast<size_t>(FilterForConTier(e.con));
            if (!settings.filters[f]) continue;
        }
        rows.push_back(e);
    }
    // Start from the server's own order so every sort below is stable over it.
    std::stable_sort(rows.begin(), rows.end(), [](const TrackEntry& a, const TrackEntry& b) {
        return a.serverOrder < b.serverOrder;
    });
    switch (settings.sort) {
        case TrackSort::Normal:
            break;
        case TrackSort::Distance:
            std::stable_sort(rows.begin(), rows.end(), [](const TrackEntry& a, const TrackEntry& b) {
                return a.distance < b.distance;
            });
            break;
        case TrackSort::RDistance:
            std::stable_sort(rows.begin(), rows.end(), [](const TrackEntry& a, const TrackEntry& b) {
                return a.distance > b.distance;
            });
            break;
        case TrackSort::Consider:  // hardest (red) first: tracking.tsv row 3
            std::stable_sort(rows.begin(), rows.end(), [](const TrackEntry& a, const TrackEntry& b) {
                return ConRank(a.con) > ConRank(b.con);
            });
            break;
        case TrackSort::RConsider:
            std::stable_sort(rows.begin(), rows.end(), [](const TrackEntry& a, const TrackEntry& b) {
                return ConRank(a.con) < ConRank(b.con);
            });
            break;
    }
    return rows;
}

std::optional<TrackSort> ParseTrackSort(const std::string& arg) {
    const std::string a = Lower(arg);
    if (a == "normal") return TrackSort::Normal;
    if (a == "distance") return TrackSort::Distance;
    if (a == "consider") return TrackSort::Consider;
    if (a == "rdistance") return TrackSort::RDistance;
    if (a == "rconsider") return TrackSort::RConsider;
    return std::nullopt;
}

std::optional<TrackPlayers> ParseTrackPlayers(const std::string& arg) {
    const std::string a = Lower(arg);
    if (a == "on") return TrackPlayers::On;
    if (a == "off") return TrackPlayers::Off;
    if (a == "group") return TrackPlayers::Group;
    return std::nullopt;
}

std::optional<TrackFilter> ParseTrackFilter(const std::string& arg) {
    const std::string a = Lower(arg);
    if (a == "red") return TrackFilter::Red;
    if (a == "yellow") return TrackFilter::Yellow;
    if (a == "white") return TrackFilter::White;
    if (a == "blue") return TrackFilter::Blue;
    if (a == "lblue") return TrackFilter::LightBlue;
    if (a == "green") return TrackFilter::Green;
    return std::nullopt;
}

float RelativeHeading512(float headingDeg, float px, float py, float tx, float ty) {
    // Bearing in client degrees: 0 = +Y, 90 = +X (MovementComponent's formula).
    float bearing = std::atan2(tx - px, ty - py) * 180.0f / 3.14159265358979f;
    if (bearing < 0.0f) bearing += 360.0f;
    // DoTracking: my_server - target_server == target_client - my_client.
    float rel = (bearing - headingDeg) * 512.0f / 360.0f;
    rel = std::fmod(rel, 512.0f);
    if (rel < 0.0f) rel += 512.0f;
    return rel;
}

TrackDirection DirectionForRelative(float rel) {
    // zone/client_process.cpp Client::DoTracking, transcribed.
    if (rel > 480.0f) return TrackDirection::StraightAhead;
    if (rel > 416.0f) return TrackDirection::AheadLeft;
    if (rel > 352.0f) return TrackDirection::Left;
    if (rel > 288.0f) return TrackDirection::BehindLeft;
    if (rel > 224.0f) return TrackDirection::Behind;
    if (rel > 160.0f) return TrackDirection::BehindRight;
    if (rel > 96.0f) return TrackDirection::Right;
    if (rel > 32.0f) return TrackDirection::AheadRight;
    return TrackDirection::StraightAhead;
}

std::string DirectionLine(const EQT::StringDatabase& strings, const std::string& name,
                          TrackDirection dir) {
    auto side = [&](uint32_t id) { return EqStrLine(strings, id); };
    switch (dir) {
        case TrackDirection::StraightAhead:
            return EqStrLine(strings, kStrStraightAhead, {name});
        case TrackDirection::Behind:
            return EqStrLine(strings, kStrBehindYou, {name});
        case TrackDirection::AheadRight:
            return EqStrLine(strings, kStrAheadAndTo, {name, side(kStrRight)});
        case TrackDirection::Right:
            return EqStrLine(strings, kStrToThe, {name, side(kStrRight)});
        case TrackDirection::BehindRight:
            return EqStrLine(strings, kStrBehindAndTo, {name, side(kStrRight)});
        case TrackDirection::AheadLeft:
            return EqStrLine(strings, kStrAheadAndTo, {name, side(kStrLeft)});
        case TrackDirection::Left:
            return EqStrLine(strings, kStrToThe, {name, side(kStrLeft)});
        case TrackDirection::BehindLeft:
            return EqStrLine(strings, kStrBehindAndTo, {name, side(kStrLeft)});
    }
    return "";
}

}  // namespace tracking
}  // namespace eqt
