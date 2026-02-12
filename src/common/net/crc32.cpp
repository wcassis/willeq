#include "common/net/crc32.h"
#include <cstring>

// Slice-by-4 CRC32 lookup tables.
// Table0 is the standard CRC32 table (polynomial 0xEDB88320, reflected).
// Tables 1-3 are derived for processing 4 bytes per iteration.
static unsigned int CRC32Tables[4][256];

static struct CRC32TablesInit {
	CRC32TablesInit() {
		// Generate Table0 from polynomial
		for (unsigned int i = 0; i < 256; ++i) {
			unsigned int crc = i;
			for (int j = 0; j < 8; ++j) {
				crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
			}
			CRC32Tables[0][i] = crc;
		}
		// Generate Tables 1-3 for slice-by-4
		for (unsigned int i = 0; i < 256; ++i) {
			CRC32Tables[1][i] = (CRC32Tables[0][i] >> 8) ^ CRC32Tables[0][CRC32Tables[0][i] & 0xFF];
			CRC32Tables[2][i] = (CRC32Tables[1][i] >> 8) ^ CRC32Tables[0][CRC32Tables[1][i] & 0xFF];
			CRC32Tables[3][i] = (CRC32Tables[2][i] >> 8) ^ CRC32Tables[0][CRC32Tables[2][i] & 0xFF];
		}
	}
} crc32TablesInit;

// Process remaining bytes one at a time
static inline unsigned int crc32_tail(unsigned int crc, const uint8_t* buffer, int count) {
	for (int i = 0; i < count; ++i) {
		crc = (crc >> 8) ^ CRC32Tables[0][(crc ^ buffer[i]) & 0xFF];
	}
	return crc;
}

// Slice-by-4: process 4 bytes per iteration using 4 lookup tables
static inline unsigned int crc32_body(unsigned int crc, const uint8_t* buffer, int size) {
	int i = 0;

	// Process 4 bytes at a time
	while (i + 4 <= size) {
		uint32_t word;
		memcpy(&word, &buffer[i], 4);  // ARM-safe unaligned read
		crc ^= word;
		crc = CRC32Tables[3][(crc      ) & 0xFF] ^
		      CRC32Tables[2][(crc >>  8) & 0xFF] ^
		      CRC32Tables[1][(crc >> 16) & 0xFF] ^
		      CRC32Tables[0][(crc >> 24) & 0xFF];
		i += 4;
	}

	// Handle remaining 0-3 bytes
	return crc32_tail(crc, &buffer[i], size - i);
}

int EQ::Crc32(const void * data, int size)
{
	unsigned int crc = 0xFFFFFFFFu;
	auto buffer = (const uint8_t *)data;
	crc = crc32_body(crc, buffer, size);
	return static_cast<int>(~crc);
}

int EQ::Crc32(const void * data, int size, int key)
{
	unsigned int crc = 0xFFFFFFFFu;

	// Mix in the key bytes first (always exactly 4 bytes)
	uint8_t keyBytes[4];
	memcpy(keyBytes, &key, 4);
	crc = crc32_tail(crc, keyBytes, 4);

	// Process data with slice-by-4
	auto buffer = (const uint8_t *)data;
	crc = crc32_body(crc, buffer, size);
	return static_cast<int>(~crc);
}
