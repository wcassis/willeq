#pragma once

#ifdef WITH_AUDIO

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace EQT {
namespace Audio {

class AudioManager;
class ZoneSoundEmitter;
class EffLoader;

// Manages all sound emitters for a zone
// Loads zone EFF files and creates/updates emitters
class ZoneAudioManager {
public:
    ZoneAudioManager();
    ~ZoneAudioManager();

    // Prevent copying
    ZoneAudioManager(const ZoneAudioManager&) = delete;
    ZoneAudioManager& operator=(const ZoneAudioManager&) = delete;

    // Set audio manager reference (must be called before loadZone)
    void setAudioManager(AudioManager* audioManager);

    // Load zone sound configuration
    // Returns true if zone has sound emitters
    bool loadZone(const std::string& zoneName, const std::string& eqPath);

    // Unload current zone
    void unloadZone();

    // Update all emitters
    // deltaTime: time since last update in seconds
    // listenerPos: player position in EQ coordinates
    // isDay: true if daytime, false if nighttime
    void update(float deltaTime, const glm::vec3& listenerPos, bool isDay);

    // Day/night transition
    void setDayNight(bool isDay);
    bool isDaytime() const { return isDay_; }

    // Current zone info
    const std::string& getCurrentZone() const { return currentZone_; }
    bool isZoneLoaded() const { return !currentZone_.empty(); }

    // Emitter statistics
    size_t getEmitterCount() const;
    size_t getActiveEmitterCount() const;
    size_t getMusicEmitterCount() const;

    // Stop all sounds (for zone transitions). `musicFadeSeconds` is how long
    // the current music takes to fade out; 0 = immediate. B1080 (#400) calls
    // this from the zone-OUT teardown so the leaving zone's music and every
    // emitter go quiet while the loading screen is up, instead of carrying
    // over until the next zone's ZoneEntry reloads the audio.
    void stopAllSounds(float musicFadeSeconds = 0.5f);

    // B1080 (#400): the zone-out teardown, in one call. Stops each emitter
    // (freeing the looped SFX channels, B1074) and fades the current music.
    // Idempotent and safe when nothing is loaded: the emitters are already
    // empty and AudioManager::stopMusic() no-ops on a player that is not
    // playing. Called from the zone session teardown (ZoneComponent), which
    // is the single point every zone-OUT path runs through -- a graceful
    // zone-out, the zone-out handshake timeout, a logout, and the B1072
    // fresh-login give-up all reach it via DisconnectFromZone().
    void silenceForZoneOut(float musicFadeSeconds);

    // Pause/resume all sounds (for menus, etc.)
    void pause();
    void resume();

private:
    // Create emitters from loaded EFF data
    void createEmittersFromEffData();

    // Find music emitter at listener position (for location-based music)
    ZoneSoundEmitter* findMusicEmitter(const glm::vec3& listenerPos);

private:
    AudioManager* audioManager_ = nullptr;
    std::unique_ptr<EffLoader> effLoader_;
    std::vector<std::unique_ptr<ZoneSoundEmitter>> emitters_;

    std::string currentZone_;
    std::string eqPath_;
    bool isDay_ = true;
    bool isPaused_ = false;

    // Currently active music emitter (for location-based zone music)
    ZoneSoundEmitter* activeMusicEmitter_ = nullptr;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
