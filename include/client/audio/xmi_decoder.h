#pragma once

#ifdef WITH_AUDIO

#include <vector>
#include <cstdint>
#include <string>
#include <utility>

namespace EQT {
namespace Audio {

// Decodes Extended MIDI (XMI) files to standard MIDI format
// XMI was used by early DOS games including EverQuest for music
// Note: This decoder only converts the format - actual MIDI playback requires
// a synthesizer like FluidSynth with a SoundFont.
class XmiDecoder {
public:
    XmiDecoder() = default;

    // Decode XMI data to standard MIDI
    // sequenceIndex: 0 = first sequence only (default), 1-N = specific sequence, -1 = all sequences combined
    // Returns empty vector on failure
    std::vector<uint8_t> decode(const std::vector<uint8_t>& xmiData, int sequenceIndex = 0);

    // Decode from file
    // sequenceIndex: 0 = first sequence only (default), 1-N = specific sequence, -1 = all sequences combined
    std::vector<uint8_t> decodeFile(const std::string& filepath, int sequenceIndex = 0);

    // Get number of sequences in last decoded XMI file
    uint16_t getNumSequences() const { return numSequences_; }

    // Get last error message
    const std::string& getError() const { return error_; }

private:
    // MIDI event for timeline-based conversion
    struct MidiEvent {
        uint32_t absoluteTime;
        std::vector<uint8_t> data;

        bool operator>(const MidiEvent& other) const {
            return absoluteTime > other.absoluteTime;
        }
    };

    // XMI file structure parsing
    bool parseHeader(const uint8_t* data, size_t size);

    // Parse all EVNT sequences and combine into single MIDI output
    bool parseAllSequences(const uint8_t* data,
        const std::vector<std::pair<size_t, size_t>>& evntChunks);

    // Parse a single EVNT chunk into events
    bool parseSequenceEvents(const uint8_t* data, size_t size,
        std::vector<MidiEvent>& events, uint32_t& duration);

    // Write MIDI output from accumulated events
    void writeMidiOutput(const std::vector<MidiEvent>& events);

    // Legacy single-sequence parser (kept for compatibility)
    bool parseSequence(const uint8_t* data, size_t size, size_t& offset);

    // Convert XMI timing to MIDI timing
    uint32_t convertTiming(uint32_t xmiTiming);

    // Write MIDI variable-length quantity
    void writeVarLen(std::vector<uint8_t>& output, uint32_t value);

    // Read XMI variable-length quantity (different from MIDI)
    uint32_t readXmiVarLen(const uint8_t* data, size_t& offset);

private:
    std::string error_;

    // XMI format info
    uint16_t numSequences_ = 0;
    uint16_t ticksPerQuarter_ = 60;  // XMI default

    // Temporary storage during conversion
    std::vector<uint8_t> midiOutput_;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
