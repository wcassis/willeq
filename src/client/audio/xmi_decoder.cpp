#ifdef WITH_AUDIO

#include "client/audio/xmi_decoder.h"
#include "common/logging.h"

#include <fstream>
#include <cstring>
#include <algorithm>
#include <queue>

namespace EQT {
namespace Audio {

// XMI file format constants
static constexpr uint32_t FORM_MAGIC = 0x4D524F46;  // "FORM"
static constexpr uint32_t XDIR_MAGIC = 0x52494458;  // "XDIR"
static constexpr uint32_t XMID_MAGIC = 0x44494D58;  // "XMID"
static constexpr uint32_t EVNT_MAGIC = 0x544E5645;  // "EVNT"
static constexpr uint32_t CAT_MAGIC  = 0x20544143;  // "CAT "

// Use MidiEvent from the class definition in header

std::vector<uint8_t> XmiDecoder::decode(const std::vector<uint8_t>& xmiData) {
    if (xmiData.size() < 12) {
        error_ = "XMI data too small";
        return {};
    }

    const uint8_t* data = xmiData.data();
    size_t size = xmiData.size();

    // Parse header
    if (!parseHeader(data, size)) {
        return {};
    }

    // Find ALL EVNT chunks (XMI can have multiple sequences)
    std::vector<std::pair<size_t, size_t>> evntChunks;  // offset, size pairs
    size_t offset = 0;
    while (offset + 8 < size) {
        uint32_t chunkId;
        std::memcpy(&chunkId, data + offset, 4);

        if (chunkId == EVNT_MAGIC) {
            size_t evntOffset = offset + 8;  // Skip "EVNT" + size
            uint32_t chunkSize;
            std::memcpy(&chunkSize, data + offset + 4, 4);
            // XMI uses big-endian for chunk sizes
            chunkSize = ((chunkSize & 0xFF) << 24) |
                       ((chunkSize & 0xFF00) << 8) |
                       ((chunkSize & 0xFF0000) >> 8) |
                       ((chunkSize & 0xFF000000) >> 24);

            if (evntOffset + chunkSize <= size) {
                evntChunks.push_back({evntOffset, chunkSize});
            }
            offset = evntOffset + chunkSize;
        } else {
            offset++;
        }
    }

    if (evntChunks.empty()) {
        error_ = "No EVNT chunk found in XMI file";
        return {};
    }

    // Parse all sequences and concatenate them
    if (!parseAllSequences(data, evntChunks)) {
        return {};
    }

    return midiOutput_;
}

std::vector<uint8_t> XmiDecoder::decodeFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        error_ = "Failed to open file: " + filepath;
        return {};
    }

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        error_ = "Failed to read file: " + filepath;
        return {};
    }

    return decode(data);
}

bool XmiDecoder::parseHeader(const uint8_t* data, size_t size) {
    if (size < 12) {
        error_ = "File too small for XMI header";
        return false;
    }

    uint32_t magic;
    std::memcpy(&magic, data, 4);

    if (magic != FORM_MAGIC) {
        error_ = "Not a valid XMI file (missing FORM header)";
        return false;
    }

    // Skip FORM size
    size_t offset = 8;

    // Check for XDIR or CAT
    std::memcpy(&magic, data + offset, 4);
    if (magic == XDIR_MAGIC) {
        // XDIR format
        numSequences_ = 1;
    } else if (magic == CAT_MAGIC) {
        // CAT format (multiple sequences)
        offset += 4;
        std::memcpy(&magic, data + offset, 4);
        if (magic != XMID_MAGIC) {
            error_ = "Invalid XMI CAT format";
            return false;
        }
        numSequences_ = 1;  // For now, just handle first sequence
    } else {
        error_ = "Unknown XMI format";
        return false;
    }

    return true;
}

// Parse all EVNT sequences and concatenate into single MIDI output
bool XmiDecoder::parseAllSequences(const uint8_t* data,
    const std::vector<std::pair<size_t, size_t>>& evntChunks) {

    midiOutput_.clear();
    std::vector<MidiEvent> allEvents;
    uint32_t timeOffset = 0;

    // Parse each sequence and accumulate events
    for (size_t i = 0; i < evntChunks.size(); i++) {
        const auto& [chunkOffset, chunkSize] = evntChunks[i];
        std::vector<MidiEvent> seqEvents;
        uint32_t seqDuration = 0;

        if (!parseSequenceEvents(data + chunkOffset, chunkSize, seqEvents, seqDuration)) {
            return false;
        }

        // Add events with time offset for this sequence
        for (auto& evt : seqEvents) {
            evt.absoluteTime += timeOffset;
            allEvents.push_back(evt);
        }

        // Next sequence starts after this one ends
        timeOffset += seqDuration;
    }

    // Sort all events by time
    std::stable_sort(allEvents.begin(), allEvents.end(),
        [](const MidiEvent& a, const MidiEvent& b) {
            return a.absoluteTime < b.absoluteTime;
        });

    // Write MIDI output
    writeMidiOutput(allEvents);
    return true;
}

// Parse a single EVNT chunk into events (without writing MIDI)
bool XmiDecoder::parseSequenceEvents(const uint8_t* data, size_t size,
    std::vector<MidiEvent>& events, uint32_t& duration) {

    events.clear();
    size_t pos = 0;
    uint32_t currentTime = 0;
    uint32_t maxTime = 0;

    // XMI timing: 120 Hz (ticks per second)
    // To match this in MIDI with PPQN=60 at default tempo 500000:
    //   ticks/sec = 60 * 1000000 / 500000 = 120 ✓
    // No multipliers needed when using these defaults

    while (pos < size) {
        uint8_t status = data[pos];

        // XMI delay: sum bytes until we hit a status byte (>= 0x80)
        // No multiplier needed with PPQN=60 and tempo=500000
        if (status < 0x80) {
            uint32_t delta = 0;
            while (pos < size && data[pos] < 0x80) {
                delta += data[pos++];
            }
            currentTime += delta;
            continue;
        }

        if (status == 0xFF) {
            // Meta event
            pos++;
            if (pos >= size) break;

            uint8_t metaType = data[pos++];
            if (pos >= size) break;

            uint32_t metaLen = readXmiVarLen(data, pos);

            // Skip tempo events - XMI timing is fixed at 120 Hz
            // We use PPQN=60 with default MIDI tempo (500000) to match
            if (metaType == 0x51) {
                pos += metaLen;
                continue;
            }

            // Skip EndOfTrack events - we'll add one at the end of the combined output
            if (metaType == 0x2F) {
                pos += metaLen;
                break;  // End of this sequence
            }

            MidiEvent evt;
            evt.absoluteTime = currentTime;
            evt.data.push_back(0xFF);
            evt.data.push_back(metaType);

            // Write length as variable length
            std::vector<uint8_t> lenBytes;
            uint32_t tempLen = metaLen;
            do {
                lenBytes.insert(lenBytes.begin(), tempLen & 0x7F);
                tempLen >>= 7;
            } while (tempLen > 0);
            for (size_t i = 0; i < lenBytes.size() - 1; i++) {
                lenBytes[i] |= 0x80;
            }
            for (uint8_t b : lenBytes) {
                evt.data.push_back(b);
            }

            for (uint32_t i = 0; i < metaLen && pos < size; i++) {
                evt.data.push_back(data[pos++]);
            }

            events.push_back(evt);
        } else if (status >= 0xF0 && status <= 0xF7) {
            // System exclusive
            pos++;
            uint32_t sysexLen = readXmiVarLen(data, pos);

            MidiEvent evt;
            evt.absoluteTime = currentTime;
            evt.data.push_back(status);

            // Write length
            std::vector<uint8_t> lenBytes;
            uint32_t tempLen = sysexLen;
            do {
                lenBytes.insert(lenBytes.begin(), tempLen & 0x7F);
                tempLen >>= 7;
            } while (tempLen > 0);
            for (size_t i = 0; i < lenBytes.size() - 1; i++) {
                lenBytes[i] |= 0x80;
            }
            for (uint8_t b : lenBytes) {
                evt.data.push_back(b);
            }

            for (uint32_t i = 0; i < sysexLen && pos < size; i++) {
                evt.data.push_back(data[pos++]);
            }

            events.push_back(evt);
        } else {
            // Channel message
            uint8_t msgType = status & 0xF0;
            uint8_t channel = status & 0x0F;
            pos++;

            if (msgType == 0xC0 || msgType == 0xD0) {
                // Program change or channel pressure (1 data byte)
                MidiEvent evt;
                evt.absoluteTime = currentTime;
                evt.data.push_back(status);
                if (pos < size) {
                    evt.data.push_back(data[pos++]);
                }
                events.push_back(evt);
            } else if (msgType == 0x90) {
                // Note on - XMI has duration following
                uint8_t note = 0;
                uint8_t velocity = 0;

                if (pos < size) note = data[pos++];
                if (pos < size) velocity = data[pos++];

                // Read XMI duration using standard MIDI VLQ encoding
                // No multiplier needed with PPQN=60 and default tempo
                uint32_t duration = readXmiVarLen(data, pos);

                // Add note-on event
                MidiEvent noteOn;
                noteOn.absoluteTime = currentTime;
                noteOn.data.push_back(status);
                noteOn.data.push_back(note);
                noteOn.data.push_back(velocity);
                events.push_back(noteOn);

                // Add note-off event at currentTime + duration
                if (velocity > 0 && duration > 0) {
                    MidiEvent noteOff;
                    noteOff.absoluteTime = currentTime + duration;
                    noteOff.data.push_back(0x80 | channel);  // Note off
                    noteOff.data.push_back(note);
                    noteOff.data.push_back(0);  // Velocity 0
                    events.push_back(noteOff);
                    if (noteOff.absoluteTime > maxTime) maxTime = noteOff.absoluteTime;
                }
            } else if (msgType == 0x80) {
                // XMI shouldn't have note-off events (durations handle this)
                // Skip the data bytes if present
                if (pos < size) pos++;
                if (pos < size) pos++;
            } else {
                // Other 2-byte messages (aftertouch, control change, pitch bend)
                MidiEvent evt;
                evt.absoluteTime = currentTime;
                evt.data.push_back(status);
                if (pos < size) evt.data.push_back(data[pos++]);
                if (pos < size) evt.data.push_back(data[pos++]);
                events.push_back(evt);
            }
        }
    }

    // Track max time for duration (also check currentTime in case no notes)
    if (currentTime > maxTime) maxTime = currentTime;
    duration = maxTime;
    return true;
}

// Write MIDI output from accumulated events
void XmiDecoder::writeMidiOutput(const std::vector<MidiEvent>& allEvents) {
    // Write MIDI header
    midiOutput_.push_back('M');
    midiOutput_.push_back('T');
    midiOutput_.push_back('h');
    midiOutput_.push_back('d');

    // Header length (6 bytes)
    midiOutput_.push_back(0);
    midiOutput_.push_back(0);
    midiOutput_.push_back(0);
    midiOutput_.push_back(6);

    // Format type (0 = single track)
    midiOutput_.push_back(0);
    midiOutput_.push_back(0);

    // Number of tracks (1)
    midiOutput_.push_back(0);
    midiOutput_.push_back(1);

    // PPQN = 60 gives 120 ticks/sec at default tempo (500000 µs/beat = 120 BPM)
    // This matches XMI's fixed 120 Hz timing
    midiOutput_.push_back(0x00);
    midiOutput_.push_back(0x3C);  // 60

    // MTrk chunk
    midiOutput_.push_back('M');
    midiOutput_.push_back('T');
    midiOutput_.push_back('r');
    midiOutput_.push_back('k');

    // Track length placeholder
    size_t trackLengthOffset = midiOutput_.size();
    midiOutput_.push_back(0);
    midiOutput_.push_back(0);
    midiOutput_.push_back(0);
    midiOutput_.push_back(0);

    size_t trackStart = midiOutput_.size();

    // Write events with delta times
    uint32_t lastTime = 0;
    bool hasEndOfTrack = false;

    for (const auto& evt : allEvents) {
        uint32_t deltaTime = evt.absoluteTime - lastTime;
        lastTime = evt.absoluteTime;

        writeVarLen(midiOutput_, deltaTime);

        for (uint8_t b : evt.data) {
            midiOutput_.push_back(b);
        }

        // Check for end of track
        if (evt.data.size() >= 2 && evt.data[0] == 0xFF && evt.data[1] == 0x2F) {
            hasEndOfTrack = true;
        }
    }

    // Add end of track if not present
    if (!hasEndOfTrack) {
        writeVarLen(midiOutput_, 0);
        midiOutput_.push_back(0xFF);
        midiOutput_.push_back(0x2F);
        midiOutput_.push_back(0x00);
    }

    // Fill in track length
    uint32_t trackLength = midiOutput_.size() - trackStart;
    midiOutput_[trackLengthOffset] = (trackLength >> 24) & 0xFF;
    midiOutput_[trackLengthOffset + 1] = (trackLength >> 16) & 0xFF;
    midiOutput_[trackLengthOffset + 2] = (trackLength >> 8) & 0xFF;
    midiOutput_[trackLengthOffset + 3] = trackLength & 0xFF;
}

uint32_t XmiDecoder::convertTiming(uint32_t xmiTiming) {
    return xmiTiming;
}

void XmiDecoder::writeVarLen(std::vector<uint8_t>& output, uint32_t value) {
    if (value == 0) {
        output.push_back(0);
        return;
    }

    uint8_t buffer[5];
    int count = 0;

    buffer[count++] = value & 0x7F;
    value >>= 7;

    while (value > 0) {
        buffer[count++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }

    // Write in reverse order
    while (count > 0) {
        output.push_back(buffer[--count]);
    }
}

uint32_t XmiDecoder::readXmiVarLen(const uint8_t* data, size_t& offset) {
    uint32_t value = 0;
    uint8_t byte;

    do {
        byte = data[offset++];
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
