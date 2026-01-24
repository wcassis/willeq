// Tool to extract individual tracks from an XMI file and convert to MIDI
// Usage: xmi_extract_tracks <input.xmi> <output_dir>

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <string>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// XMI file format constants
static constexpr uint32_t FORM_MAGIC = 0x4D524F46;  // "FORM"
static constexpr uint32_t EVNT_MAGIC = 0x544E5645;  // "EVNT"

struct MidiEvent {
    uint32_t absoluteTime;
    std::vector<uint8_t> data;
};

uint32_t readBigEndian32(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

uint32_t readXmiVarLen(const uint8_t* data, size_t& offset) {
    uint32_t value = 0;
    uint8_t byte;
    do {
        byte = data[offset++];
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return value;
}

void writeVarLen(std::vector<uint8_t>& output, uint32_t value) {
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
    while (count > 0) {
        output.push_back(buffer[--count]);
    }
}

bool parseEvntChunk(const uint8_t* data, size_t size, std::vector<MidiEvent>& events, uint32_t& duration) {
    events.clear();
    size_t pos = 0;
    uint32_t currentTime = 0;
    uint32_t maxTime = 0;

    while (pos < size) {
        uint8_t status = data[pos];

        // XMI delay
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

            // Skip tempo events
            if (metaType == 0x51) {
                pos += metaLen;
                continue;
            }
            // End of track
            if (metaType == 0x2F) {
                pos += metaLen;
                break;
            }

            MidiEvent evt;
            evt.absoluteTime = currentTime;
            evt.data.push_back(0xFF);
            evt.data.push_back(metaType);

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
                MidiEvent evt;
                evt.absoluteTime = currentTime;
                evt.data.push_back(status);
                if (pos < size) evt.data.push_back(data[pos++]);
                events.push_back(evt);
            } else if (msgType == 0x90) {
                uint8_t note = 0, velocity = 0;
                if (pos < size) note = data[pos++];
                if (pos < size) velocity = data[pos++];
                uint32_t noteDuration = readXmiVarLen(data, pos);

                MidiEvent noteOn;
                noteOn.absoluteTime = currentTime;
                noteOn.data.push_back(status);
                noteOn.data.push_back(note);
                noteOn.data.push_back(velocity);
                events.push_back(noteOn);

                if (velocity > 0 && noteDuration > 0) {
                    MidiEvent noteOff;
                    noteOff.absoluteTime = currentTime + noteDuration;
                    noteOff.data.push_back(0x80 | channel);
                    noteOff.data.push_back(note);
                    noteOff.data.push_back(0);
                    events.push_back(noteOff);
                    if (noteOff.absoluteTime > maxTime) maxTime = noteOff.absoluteTime;
                }
            } else if (msgType == 0x80) {
                if (pos < size) pos++;
                if (pos < size) pos++;
            } else {
                MidiEvent evt;
                evt.absoluteTime = currentTime;
                evt.data.push_back(status);
                if (pos < size) evt.data.push_back(data[pos++]);
                if (pos < size) evt.data.push_back(data[pos++]);
                events.push_back(evt);
            }
        }
    }

    if (currentTime > maxTime) maxTime = currentTime;
    duration = maxTime;
    return true;
}

std::vector<uint8_t> writeMidi(const std::vector<MidiEvent>& events) {
    std::vector<uint8_t> output;

    // MIDI header
    output.push_back('M'); output.push_back('T');
    output.push_back('h'); output.push_back('d');
    output.push_back(0); output.push_back(0);
    output.push_back(0); output.push_back(6);
    output.push_back(0); output.push_back(0);  // Format 0
    output.push_back(0); output.push_back(1);  // 1 track
    output.push_back(0); output.push_back(0x3C);  // PPQN = 60

    // MTrk
    output.push_back('M'); output.push_back('T');
    output.push_back('r'); output.push_back('k');

    size_t trackLengthOffset = output.size();
    output.push_back(0); output.push_back(0);
    output.push_back(0); output.push_back(0);

    size_t trackStart = output.size();

    // Sort events by time
    std::vector<MidiEvent> sortedEvents = events;
    std::stable_sort(sortedEvents.begin(), sortedEvents.end(),
        [](const MidiEvent& a, const MidiEvent& b) {
            return a.absoluteTime < b.absoluteTime;
        });

    uint32_t lastTime = 0;
    for (const auto& evt : sortedEvents) {
        uint32_t deltaTime = evt.absoluteTime - lastTime;
        lastTime = evt.absoluteTime;
        writeVarLen(output, deltaTime);
        for (uint8_t b : evt.data) {
            output.push_back(b);
        }
    }

    // End of track
    writeVarLen(output, 0);
    output.push_back(0xFF);
    output.push_back(0x2F);
    output.push_back(0x00);

    // Fill in track length
    uint32_t trackLength = output.size() - trackStart;
    output[trackLengthOffset] = (trackLength >> 24) & 0xFF;
    output[trackLengthOffset + 1] = (trackLength >> 16) & 0xFF;
    output[trackLengthOffset + 2] = (trackLength >> 8) & 0xFF;
    output[trackLengthOffset + 3] = trackLength & 0xFF;

    return output;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.xmi> <output_dir>" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputDir = argv[2];

    // Read input file
    std::ifstream file(inputFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << inputFile << std::endl;
        return 1;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    // Create output directory
    fs::create_directories(outputDir);

    // Find all EVNT chunks
    std::vector<std::pair<size_t, size_t>> evntChunks;
    size_t offset = 0;
    while (offset + 8 < fileSize) {
        uint32_t chunkId;
        std::memcpy(&chunkId, data.data() + offset, 4);

        if (chunkId == EVNT_MAGIC) {
            uint32_t chunkSize;
            std::memcpy(&chunkSize, data.data() + offset + 4, 4);
            // Big-endian
            chunkSize = ((chunkSize & 0xFF) << 24) |
                       ((chunkSize & 0xFF00) << 8) |
                       ((chunkSize & 0xFF0000) >> 8) |
                       ((chunkSize & 0xFF000000) >> 24);

            size_t evntOffset = offset + 8;
            if (evntOffset + chunkSize <= fileSize) {
                evntChunks.push_back({evntOffset, chunkSize});
            }
            offset = evntOffset + chunkSize;
        } else {
            offset++;
        }
    }

    std::cout << "Found " << evntChunks.size() << " tracks in " << inputFile << std::endl;

    // Extract each track
    for (size_t i = 0; i < evntChunks.size(); i++) {
        const auto& [chunkOffset, chunkSize] = evntChunks[i];

        std::vector<MidiEvent> events;
        uint32_t duration;

        if (!parseEvntChunk(data.data() + chunkOffset, chunkSize, events, duration)) {
            std::cerr << "Failed to parse track " << (i + 1) << std::endl;
            continue;
        }

        std::vector<uint8_t> midi = writeMidi(events);

        // Write MIDI file
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/track_%02zu.mid", outputDir.c_str(), i + 1);

        std::ofstream outFile(filename, std::ios::binary);
        if (outFile.is_open()) {
            outFile.write(reinterpret_cast<const char*>(midi.data()), midi.size());
            outFile.close();
            std::cout << "Wrote " << filename << " (" << events.size() << " events, duration: " << duration << " ticks)" << std::endl;
        } else {
            std::cerr << "Failed to write " << filename << std::endl;
        }
    }

    std::cout << "\nExtraction complete. MIDI files in: " << outputDir << std::endl;
    return 0;
}
