/*
 * captured_packets.h -- harness for pinning packet structs to REAL OBSERVED
 * BYTES rather than to sizeof().
 *
 * Why this exists (T147, generalising T144):
 *
 *   Every wire bug this programme has found survived a *passing* static_assert.
 *   `Object_Struct` asserted 100 bytes for as long as the repo existed and was
 *   wrong from offset 8 onward. `EnvDamage2_Struct` asserted 32 for a 31-byte
 *   packet. A size assert only proves the fields you wrote sum to the number
 *   you wrote next to them -- it agrees with the code it is supposed to guard.
 *
 *   The only assertion that has ever caught this class is one anchored OUTSIDE
 *   the client: bytes captured off a live server, decoded field by field, and
 *   cross-checked against an authority the client does not control (T144 used
 *   the server's own `object` DB table).
 *
 * How to add a capture:
 *
 *   1. Capture the packet. Log `EQ::Net::Packet::ToString()` at the top of the
 *      handler, run a live session, take the hexdump. Put the annotated dump
 *      in review/<UNIT>/ -- the bytes in this header are the evidence, the
 *      dump is the working.
 *   2. Paste the bytes as a `CapturedPacket` below, INCLUDING the 2-byte
 *      opcode, and fill in `provenance` with where and when it came from and
 *      what independently corroborates the expected values.
 *   3. Write a test using ExpectFields() naming every field you claim to know,
 *      by offset. Fields you have NOT corroborated: leave them out rather than
 *      pinning a guess.
 *
 * If one of these tests fails, the struct changed. Do not "fix" the test.
 */

#ifndef EQT_TESTS_CAPTURED_PACKETS_H
#define EQT_TESTS_CAPTURED_PACKETS_H

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace eqt_test {

// One packet as it appeared on the wire, opcode included.
struct CapturedPacket {
    const char*    name;        // human label, used in failure messages
    uint16_t       opcode;      // expected little-endian opcode in bytes[0..1]
    const uint8_t* bytes;       // full packet INCLUDING the 2-byte opcode
    size_t         size;        // sizeof(bytes)
    const char*    provenance;  // where it came from + what corroborates it
};

// The body is everything after the 2-byte opcode -- the region a wire struct
// is cast over.
inline const uint8_t* Body(const CapturedPacket& c) { return c.bytes + 2; }
inline size_t BodyLen(const CapturedPacket& c) { return c.size - 2; }

// Little-endian field readers. Deliberately memcpy-based: reading through a
// pointer cast at an unaligned offset is UB, and these captures are packed.
template <typename T>
inline T ReadAt(const CapturedPacket& c, size_t off) {
    T v{};
    EXPECT_LE(off + sizeof(T), BodyLen(c))
        << c.name << ": field at body offset " << off << " runs past the packet";
    if (off + sizeof(T) > BodyLen(c)) return v;
    std::memcpy(&v, Body(c) + off, sizeof(T));
    return v;
}

// NUL-terminated string field, bounded by the packet, never fixed-width.
// (T144: the bytes after a name's NUL are the server's uninitialised heap.)
inline std::string ReadCStr(const CapturedPacket& c, size_t off) {
    if (off >= BodyLen(c)) return {};
    const char* p = reinterpret_cast<const char*>(Body(c) + off);
    size_t max = BodyLen(c) - off;
    size_t n = 0;
    while (n < max && p[n] != '\0') ++n;
    return std::string(p, n);
}

// One expected value at one offset. Build a table of these per capture.
enum class FieldType { U8, U16, U32, I32, F32, CStr };

struct PinnedField {
    const char* field;      // field name, for the failure message
    size_t      offset;     // body offset (i.e. wire offset)
    FieldType   type;
    double      number;     // expected value for numeric types
    const char* text;       // expected value for CStr
};

inline PinnedField U8 (const char* f, size_t o, uint32_t v){ return {f,o,FieldType::U8 ,double(v),nullptr}; }
inline PinnedField U16(const char* f, size_t o, uint32_t v){ return {f,o,FieldType::U16,double(v),nullptr}; }
inline PinnedField U32(const char* f, size_t o, uint32_t v){ return {f,o,FieldType::U32,double(v),nullptr}; }
inline PinnedField I32(const char* f, size_t o, int32_t  v){ return {f,o,FieldType::I32,double(v),nullptr}; }
inline PinnedField F32(const char* f, size_t o, float    v){ return {f,o,FieldType::F32,double(v),nullptr}; }
inline PinnedField Str(const char* f, size_t o, const char* v){ return {f,o,FieldType::CStr,0.0,v}; }

// Assert the opcode is what the capture claims. A capture whose opcode does
// not match was pasted from the wrong packet.
inline void ExpectOpcode(const CapturedPacket& c) {
    ASSERT_GE(c.size, 2u) << c.name << ": capture is too short to hold an opcode";
    uint16_t op = static_cast<uint16_t>(c.bytes[0] | (c.bytes[1] << 8));
    EXPECT_EQ(op, c.opcode) << c.name << ": opcode mismatch -- wrong capture?"
                            << "\nprovenance: " << c.provenance;
}

// Assert every pinned field decodes to its corroborated value.
inline void ExpectFields(const CapturedPacket& c, const std::vector<PinnedField>& fields) {
    for (const PinnedField& f : fields) {
        SCOPED_TRACE(testing::Message()
                     << c.name << "." << f.field << " @ wire offset " << f.offset
                     << "\nprovenance: " << c.provenance);
        switch (f.type) {
            case FieldType::U8:   EXPECT_EQ(ReadAt<uint8_t >(c, f.offset), uint8_t (f.number)); break;
            case FieldType::U16:  EXPECT_EQ(ReadAt<uint16_t>(c, f.offset), uint16_t(f.number)); break;
            case FieldType::U32:  EXPECT_EQ(ReadAt<uint32_t>(c, f.offset), uint32_t(f.number)); break;
            case FieldType::I32:  EXPECT_EQ(ReadAt<int32_t >(c, f.offset), int32_t (f.number)); break;
            case FieldType::F32:  EXPECT_FLOAT_EQ(ReadAt<float>(c, f.offset), float(f.number)); break;
            case FieldType::CStr: EXPECT_EQ(ReadCStr(c, f.offset), std::string(f.text)); break;
        }
    }
}

// ===========================================================================
// The captures.
// ===========================================================================

// --- OP_GroundSpawn (0x0f47), Object_Struct ---
// Captured by T144 from a live eqemu session in qeynos, 2026-08-19.
// Annotated dump: /mnt/projects/fable/review/T144/groundspawn_hexdump_annotated.txt
// Expected values are the SERVER'S OWN `object` DB rows for zoneid=1, i.e. an
// authority entirely outside this client.

inline constexpr uint8_t kGroundSpawnKilnBytes[] = {
    0x47, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xc7, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x31, 0xc3, 0x00, 0x00, 0xf2, 0x43, 0x49, 0x54,
    0x31, 0x30, 0x38, 0x30, 0x34, 0x5f, 0x41, 0x43, 0x54, 0x4f, 0x52, 0x44,
    0x45, 0x46, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xfe, 0xff, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xbd, 0x01, 0x00, 0x00,
    0x01,
};

inline constexpr uint8_t kGroundSpawnForgeBytes[] = {
    0x47, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xbe, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xb8, 0x43, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x94, 0xc3, 0x00, 0x00, 0x33, 0x43, 0x49, 0x54,
    0x36, 0x36, 0x5f, 0x41, 0x43, 0x54, 0x4f, 0x52, 0x44, 0x45, 0x46, 0x00,
    0x5f, 0x41, 0x43, 0x54, 0x4f, 0x52, 0x44, 0x45, 0x46, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

inline constexpr uint8_t kGroundSpawnItemBytes[] = {
    0x47, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xbd, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9a, 0x99,
    0x13, 0xc2, 0x00, 0x00, 0x1b, 0x43, 0x33, 0xb3, 0x10, 0xc3, 0x49, 0x54,
    0x36, 0x33, 0x00, 0xc3, 0x00, 0x00, 0x33, 0x43, 0x49, 0x54, 0x36, 0x36,
    0x5f, 0x41, 0x43, 0x54, 0x4f, 0x52, 0x44, 0x45, 0x46, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
};

inline const CapturedPacket kGroundSpawnKiln{
    "OP_GroundSpawn/kiln(IT10804_ACTORDEF)", 0x0f47,
    kGroundSpawnKilnBytes, sizeof(kGroundSpawnKilnBytes),
    "T144 live qeynos capture 2026-08-19; values corroborated by eqemu DB "
    "`object` row 228452 (zoneid=1): name IT10804_ACTORDEF, type 17, "
    "x=-177, y=484, heading=117. review/T144/"};

inline const CapturedPacket kGroundSpawnForge{
    "OP_GroundSpawn/forge(IT66_ACTORDEF)", 0x0f47,
    kGroundSpawnForgeBytes, sizeof(kGroundSpawnForgeBytes),
    "T144 live qeynos capture 2026-08-19; values corroborated by eqemu DB "
    "`object` row 201 (zoneid=1): name IT66_ACTORDEF, type 17, x=-296, "
    "y=179, heading=369. Carries leftover heap after the name's NUL. "
    "review/T144/"};

inline const CapturedPacket kGroundSpawnItem{
    "OP_GroundSpawn/dropped item(IT63)", 0x0f47,
    kGroundSpawnItemBytes, sizeof(kGroundSpawnItemBytes),
    "T144 live qeynos capture 2026-08-19; a genuine dropped ground item with "
    "no DB row -- the 97-byte packet an old comment dismissed as server "
    "jitter. review/T144/"};

}  // namespace eqt_test

#endif  // EQT_TESTS_CAPTURED_PACKETS_H
