#include "client/spell/spells_eff_reader.h"

#include <cctype>
#include <cstring>
#include <fstream>

namespace EQ {

bool SpellsEffEmitter::hasContent() const {
    for (const auto& name : sprite_names) {
        if (!name.empty()) return true;
    }
    return !attach_tag.empty();
}

bool SpellsEffRecord::hasContent() const {
    for (const auto& e : emitters) {
        if (e.hasContent()) return true;
    }
    return false;
}

std::string SpellsEffReader::readFixedString32(const std::vector<uint8_t>& data, size_t offset) {
    // 32-byte field, NUL-terminated (or fully occupied). Bytes past the
    // first NUL are ignored, matching how the client's C string reads
    // would have treated it.
    size_t len = 0;
    while (len < 32 && data[offset + len] != 0) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(&data[offset]), len);
}

bool SpellsEffReader::parseRecord(const std::vector<uint8_t>& data, size_t record_index,
                                   SpellsEffRecord& out) const {
    const size_t base = record_index * kSpellsEffRecordSize;

    int32_t header_a = 0, header_b = 0;
    std::memcpy(&header_a, &data[base + 0], sizeof(int32_t));
    std::memcpy(&header_b, &data[base + 4], sizeof(int32_t));
    out.header_a = header_a;
    out.header_b = header_b;

    for (size_t b = 0; b < kSpellsEffEmittersPerRecord; ++b) {
        const size_t block_base = base + kSpellsEffRecordHeaderBytes + b * kSpellsEffEmitterSize;
        SpellsEffEmitter& emitter = out.emitters[b];

        for (size_t s = 0; s < 3; ++s) {
            emitter.sprite_names[s] = readFixedString32(data, block_base + s * 32);
        }
        emitter.attach_tag = readFixedString32(data, block_base + 3 * 32);

        std::memcpy(emitter.unknown_bytes.data(),
                    &data[block_base + kSpellsEffEmitterStringBytes],
                    kSpellsEffEmitterUnknownBytes);
    }

    return true;
}

bool SpellsEffReader::load(const std::string& path) {
    m_records.clear();
    m_loaded = false;
    m_load_error.clear();

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        m_load_error = "Failed to open spells.eff: " + path;
        return false;
    }

    const std::streamoff file_size = file.tellg();
    if (file_size < 0 || static_cast<size_t>(file_size) != kSpellsEffFileSize) {
        m_load_error = "spells.eff size mismatch: expected " +
                        std::to_string(kSpellsEffFileSize) + " bytes (256 x 2792), got " +
                        std::to_string(file_size) +
                        ". Refusing to guess at a different layout -- see T70 STOP conditions.";
        return false;
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(data.data()), file_size)) {
        m_load_error = "Failed to read spells.eff: " + path;
        return false;
    }

    m_records.resize(kSpellsEffRecordCount);
    for (size_t i = 0; i < kSpellsEffRecordCount; ++i) {
        if (!parseRecord(data, i, m_records[i])) {
            m_load_error = "Failed to parse spells.eff record " + std::to_string(i);
            m_records.clear();
            return false;
        }
    }

    m_loaded = true;
    return true;
}

const SpellsEffRecord* SpellsEffReader::getRecord(int index) const {
    if (!m_loaded) return nullptr;
    if (index < 0 || static_cast<size_t>(index) >= kSpellsEffRecordCount) return nullptr;
    return &m_records[static_cast<size_t>(index)];
}

size_t SpellsEffReader::populatedRecordCount() const {
    size_t count = 0;
    for (const auto& r : m_records) {
        if (r.hasContent()) ++count;
    }
    return count;
}

std::string SpellsEffReader::effectNameToTexture(const std::string& effect_name) {
    // GEN[A-Z][1-9]0_SPRITE -> spel[a-z][1-9].tga. See the header comment
    // and the T70 Review for exactly which observed names this does and
    // does not resolve to a real file.
    if (effect_name.size() < 5 || effect_name.compare(0, 3, "GEN") != 0) {
        return "";
    }

    char letter = effect_name[3];
    char digit = effect_name[4];
    if (letter < 'A' || letter > 'Z') return "";
    if (digit < '1' || digit > '9') return "";  // '0' deliberately excluded -- never resolves

    std::string texture = "spel";
    texture += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    texture += digit;
    texture += ".tga";
    return texture;
}

}  // namespace EQ
