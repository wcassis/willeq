#!/usr/bin/env python3
"""H03: Tiny reader for the session_log.h binary format.

Counts Zone-stream (SessionStream::Zone == 2) records in a .sleq file,
so record_tour.sh can verify the recording is non-trivial without needing
a compiled C++ utility. Layout matches include/common/net/session_log.h
exactly (little-endian, packed) — see that header for the authoritative
format; keep this in sync with it if the format ever changes.
"""
import struct
import sys

FILE_HEADER_SIZE = 112  # SessionLogFileHeader
RECORD_HEADER_FMT = "<QBBHI"  # timestampUs, stream, direction, opcode, size
RECORD_HEADER_SIZE = struct.calcsize(RECORD_HEADER_FMT)
assert RECORD_HEADER_SIZE == 16, RECORD_HEADER_SIZE

STREAM_ZONE = 2


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <session.sleq>", file=sys.stderr)
        return 2

    with open(sys.argv[1], "rb") as f:
        header = f.read(FILE_HEADER_SIZE)
        if len(header) != FILE_HEADER_SIZE:
            print("0")
            return 0
        magic = struct.unpack_from("<I", header, 0)[0]
        if magic != 0x51454C53:  # "SLEQ"
            print(f"error: bad magic {magic:#x}", file=sys.stderr)
            return 1

        total = 0
        zone_count = 0
        while True:
            rec_header = f.read(RECORD_HEADER_SIZE)
            if len(rec_header) < RECORD_HEADER_SIZE:
                break
            _ts, stream, _direction, _opcode, size = struct.unpack(
                RECORD_HEADER_FMT, rec_header
            )
            payload = f.read(size)
            if len(payload) < size:
                break
            total += 1
            if stream == STREAM_ZONE:
                zone_count += 1

        print(zone_count)
        return 0


if __name__ == "__main__":
    sys.exit(main())
