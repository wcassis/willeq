// test_fluidsynth_midi.cpp
// Simple FluidSynth MIDI streaming test
//
// Build: g++ -o test_midi test_fluidsynth_midi.cpp -lfluidsynth
// Run:   PULSE_SINK=your_sink_name ./test_midi /path/to/soundfont.sf2 /path/to/file.xmi
//
// For VLC streaming test:
//   1. Start the audio sink:
//      pactl load-module module-null-sink sink_name=midi_test
//   2. Start ffmpeg streaming:
//      ffmpeg -f pulse -i midi_test.monitor -acodec libmp3lame -ab 128k -f mp3 -listen 1 http://0.0.0.0:8085
//   3. Run this test:
//      PULSE_SINK=midi_test ./build/bin/test_fluidsynth_midi /usr/share/sounds/sf2/FluidR3_GM.sf2 /path/to/file.xmi
//   4. Listen in VLC:
//      vlc http://localhost:8085

#include <iostream>
#include <vector>
#include <cstdint>
#include <fstream>
#include <thread>
#include <chrono>
#include <csignal>

#ifdef WITH_FLUIDSYNTH
#include <fluidsynth.h>
#endif

#ifdef WITH_AUDIO
#include "client/audio/xmi_decoder.h"
#endif

static volatile bool g_running = true;

void signalHandler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
#ifndef WITH_FLUIDSYNTH
    std::cerr << "ERROR: FluidSynth not compiled in (WITH_FLUIDSYNTH not defined)\n";
    return 1;
#else
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <soundfont.sf2> <music.xmi|music.mid>\n";
        std::cerr << "\nExample:\n";
        std::cerr << "  " << argv[0] << " /usr/share/sounds/sf2/FluidR3_GM.sf2 /path/to/EverQuest/qeynos.xmi\n";
        std::cerr << "\nFor streaming via VLC:\n";
        std::cerr << "  1. pactl load-module module-null-sink sink_name=midi_test\n";
        std::cerr << "  2. ffmpeg -f pulse -i midi_test.monitor -acodec libmp3lame -ab 128k -f mp3 -listen 1 http://0.0.0.0:8085\n";
        std::cerr << "  3. PULSE_SINK=midi_test " << argv[0] << " <sf2> <xmi>\n";
        std::cerr << "  4. vlc http://localhost:8085\n";
        return 1;
    }

    const char* soundFontPath = argv[1];
    const char* musicPath = argv[2];

    // Install signal handler for clean shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "=== FluidSynth MIDI Streaming Test ===\n";
    std::cout << "SoundFont: " << soundFontPath << "\n";
    std::cout << "Music: " << musicPath << "\n";

    // Check PULSE_SINK env var
    const char* pulseSink = std::getenv("PULSE_SINK");
    if (pulseSink) {
        std::cout << "PULSE_SINK: " << pulseSink << "\n";
    } else {
        std::cout << "PULSE_SINK: (not set - using default output)\n";
    }
    std::cout << "\n";

    // Create FluidSynth settings
    std::cout << "[1/5] Creating FluidSynth settings...\n";
    fluid_settings_t* settings = new_fluid_settings();
    if (!settings) {
        std::cerr << "ERROR: Failed to create FluidSynth settings\n";
        return 1;
    }

    // Configure for PulseAudio output
    fluid_settings_setstr(settings, "audio.driver", "pulseaudio");
    fluid_settings_setnum(settings, "synth.sample-rate", 48000.0);  // Match PulseAudio default
    fluid_settings_setint(settings, "synth.reverb.active", 1);
    fluid_settings_setint(settings, "synth.chorus.active", 1);
    fluid_settings_setint(settings, "synth.polyphony", 256);

    // Audio buffer settings for stable streaming
    fluid_settings_setint(settings, "audio.period-size", 512);
    fluid_settings_setint(settings, "audio.periods", 4);

    // Set gain (volume)
    fluid_settings_setnum(settings, "synth.gain", 0.6);

    std::cout << "[2/5] Creating FluidSynth synthesizer...\n";
    fluid_synth_t* synth = new_fluid_synth(settings);
    if (!synth) {
        std::cerr << "ERROR: Failed to create FluidSynth synthesizer\n";
        delete_fluid_settings(settings);
        return 1;
    }

    std::cout << "[3/5] Loading SoundFont: " << soundFontPath << "...\n";
    int sfId = fluid_synth_sfload(synth, soundFontPath, 1);
    if (sfId < 0) {
        std::cerr << "ERROR: Failed to load SoundFont: " << soundFontPath << "\n";
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }
    std::cout << "       SoundFont loaded (ID: " << sfId << ")\n";

    std::cout << "[4/5] Creating audio driver (PulseAudio)...\n";
    fluid_audio_driver_t* audioDriver = new_fluid_audio_driver(settings, synth);
    if (!audioDriver) {
        std::cerr << "ERROR: Failed to create PulseAudio driver\n";
        std::cerr << "       Make sure PulseAudio is running\n";

        // Try ALSA as fallback
        std::cout << "       Trying ALSA fallback...\n";
        fluid_settings_setstr(settings, "audio.driver", "alsa");
        audioDriver = new_fluid_audio_driver(settings, synth);
        if (!audioDriver) {
            std::cerr << "ERROR: ALSA fallback also failed\n";
            fluid_synth_sfunload(synth, sfId, 1);
            delete_fluid_synth(synth);
            delete_fluid_settings(settings);
            return 1;
        }
        std::cout << "       Using ALSA audio driver\n";
    } else {
        std::cout << "       PulseAudio driver created\n";
    }

    // Determine if file is XMI or MIDI
    std::string path(musicPath);
    std::vector<uint8_t> midiData;

    if (path.size() >= 4 && (path.substr(path.size() - 4) == ".xmi" || path.substr(path.size() - 4) == ".XMI")) {
#ifdef WITH_AUDIO
        std::cout << "[5/5] Decoding XMI to MIDI...\n";
        EQT::Audio::XmiDecoder decoder;
        midiData = decoder.decodeFile(musicPath);
        if (midiData.empty()) {
            std::cerr << "ERROR: Failed to decode XMI: " << decoder.getError() << "\n";
            delete_fluid_audio_driver(audioDriver);
            fluid_synth_sfunload(synth, sfId, 1);
            delete_fluid_synth(synth);
            delete_fluid_settings(settings);
            return 1;
        }
        std::cout << "       Decoded " << midiData.size() << " bytes of MIDI data\n";
#else
        std::cerr << "ERROR: XMI decoding requires WITH_AUDIO (XmiDecoder)\n";
        delete_fluid_audio_driver(audioDriver);
        fluid_synth_sfunload(synth, sfId, 1);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
#endif
    } else {
        // Load MIDI file directly
        std::cout << "[5/5] Loading MIDI file...\n";
        std::ifstream file(musicPath, std::ios::binary);
        if (!file) {
            std::cerr << "ERROR: Failed to open MIDI file: " << musicPath << "\n";
            delete_fluid_audio_driver(audioDriver);
            fluid_synth_sfunload(synth, sfId, 1);
            delete_fluid_synth(synth);
            delete_fluid_settings(settings);
            return 1;
        }
        midiData = std::vector<uint8_t>(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        );
        std::cout << "       Loaded " << midiData.size() << " bytes\n";
    }

    // Create and configure player
    std::cout << "\nCreating MIDI player...\n";
    fluid_player_t* player = new_fluid_player(synth);
    if (!player) {
        std::cerr << "ERROR: Failed to create MIDI player\n";
        delete_fluid_audio_driver(audioDriver);
        fluid_synth_sfunload(synth, sfId, 1);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    // Load MIDI data
    if (fluid_player_add_mem(player, midiData.data(), midiData.size()) != FLUID_OK) {
        std::cerr << "ERROR: Failed to load MIDI data into player\n";
        delete_fluid_player(player);
        delete_fluid_audio_driver(audioDriver);
        fluid_synth_sfunload(synth, sfId, 1);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        return 1;
    }

    // Enable looping
    fluid_player_set_loop(player, -1);

    // Start playback
    std::cout << "\n*** Starting MIDI playback (Ctrl+C to stop) ***\n\n";
    fluid_player_play(player);

    // Wait for playback or interrupt
    while (g_running && fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup
    std::cout << "\nStopping playback...\n";
    fluid_player_stop(player);
    fluid_player_join(player);

    delete_fluid_player(player);
    delete_fluid_audio_driver(audioDriver);
    fluid_synth_sfunload(synth, sfId, 1);
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);

    std::cout << "Done.\n";
    return 0;
#endif
}
