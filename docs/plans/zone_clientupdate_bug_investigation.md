# Zone-In ClientUpdate Bug Investigation

**Status**: IN PROGRESS
**Last Updated**: 2026-01-21
**Branch**: modetail

## The Bug

Zone-in hangs at 40% "Finalizing connection..." because the client never receives the ClientUpdate packet for its own spawn_id.

## CONFIRMED FACTS (DO NOT RE-VERIFY)

1. **The server ABSOLUTELY sends the ClientUpdate packet** - Confirmed via tcpdump/pcap capture
2. **The ClientUpdate is inside a compressed Combined packet** - Format: `00 03 5a 78 01 ...`
3. **Other ClientUpdate packets for other spawn_ids ARE delivered correctly**
4. **The official EQ client works fine** - All fixes must be in willeq
5. **This is a local environment** - No network issues, retransmissions are our fault

## Packet Format

The missing ClientUpdate packet is sent by the zone server in this format:

```
Raw packet: 00 03 5a 78 01 35 91 bd 4b 82 51 14 c6 ...
            |--| |--| |--------zlib data---------|
             |    |
             |    +-- 5a = compression marker, 78 = zlib header start
             +------- 00 03 = Daybreak OP_Combined opcode
```

After decompression, the Combined packet contains multiple subpackets including:
```
... 18 cb 14 26 02 00 c0 59 00 e8 f9 07 00 ac 03 00 00 00 c0 4e 00 00 00 00 00
    |  |---| |---|
    |    |     +-- spawn_id (little-endian): 0x0226 = 550
    |    +-------- ClientUpdate opcode: 0x14cb
    +------------- subpacket length byte
```

## What Works vs What Doesn't

| Zone | Result | Notes |
|------|--------|-------|
| qeynos2 (summonah.json) | WORKS | Zone-in completes, ClientUpdate received |
| gfaydark (wimplo.json) | FAILS | Stuck at 40%, ClientUpdate never received |

## Key Observations

1. **RAW_RECV logging shows 1183 packets received** - no drops at entry point
2. **No CRC failures logged**
3. **No DECOMPRESS_FAIL logged**
4. **Other spawn_ids' ClientUpdate packets ARE delivered** (95, 157, 273, 272, etc.)
5. **SpawnAppearance for our spawn_id IS received** - so we're not completely blind to our entity
6. **HP updates for our spawn_id ARE received** - packets with our spawn_id do get through

## Hypothesis

The compressed Combined packet containing our ClientUpdate is either:
1. Not being decompressed correctly
2. Being processed but the subpacket parsing fails silently
3. Coming through a code path we haven't added logging to

## Code Locations

- **ProcessPacket**: `src/common/net/daybreak_connection.cpp:498` - Entry point for all packets
- **Decompress**: `src/common/net/daybreak_connection.cpp:1364` - Handles 0x5a compression marker
- **OP_Combined handling**: `src/common/net/daybreak_connection.cpp:740` - Parses subpackets
- **Fragment assembly**: Multiple locations around lines 640-1010

## Logging Added

1. `RAW_RECV` - Logs first 4 bytes of every packet received
2. `DROPPED` - Logs when packets are dropped (CRC fail, too short, etc.)
3. `DECOMPRESS_FAIL` - Logs when zlib inflate returns 0
4. Fragment assembly logging with first_byte
5. Combined subpacket logging with opcode and spawn_id

## Test Commands

```bash
# Run willeq with debug logging
timeout 10 ./build/bin/willeq -c /home/user/projects/claude/wimplo.json --no-graphics --debug 5 > /tmp/willeq_test.log 2>&1

# Capture traffic simultaneously
sudo tcpdump -i any -w /tmp/willeq_cap.pcap "udp" &

# Search for ClientUpdate in pcap
python3 /tmp/find_clientupdate.py  # Script that decompresses and searches
```

## Python Script to Find ClientUpdate in PCAP

```python
import zlib
import subprocess

result = subprocess.run([
    "tshark", "-r", "/tmp/willeq_cap.pcap",
    "-Y", "udp.srcport == 7007",
    "-T", "fields", "-e", "data.data"
], capture_output=True, text=True)

target_spawn_id = 550  # UPDATE THIS for each test run
clientupdate_opcode = 0x14cb

seen = set()
for line in result.stdout.strip().split('\n'):
    if not line or line in seen:
        continue
    seen.add(line)

    try:
        data = bytes.fromhex(line)
    except:
        continue

    # Check if it's a Combined packet with compression (00 03 5a 78)
    if len(data) >= 5 and data[0] == 0x00 and data[1] == 0x03 and data[2] == 0x5a:
        try:
            decompressed = zlib.decompress(data[3:])
            pos = 0
            while True:
                pos = decompressed.find(bytes([0xcb, 0x14]), pos)
                if pos == -1:
                    break
                if pos + 4 <= len(decompressed):
                    spawn_id = int.from_bytes(decompressed[pos+2:pos+4], 'little')
                    if spawn_id == target_spawn_id:
                        print(f"FOUND! ClientUpdate for spawn_id={spawn_id}")
                        print(f"  Raw packet: {data[:20].hex()}")
                        print(f"  Context: {decompressed[max(0,pos-4):pos+24].hex()}")
                pos += 1
        except:
            pass

print(f"Searched {len(seen)} unique packets")
```

## Next Steps

1. **Find the exact packet in RAW_RECV logs** - Match the pcap packet to willeq's received packets
2. **Trace the packet through ProcessPacket** - Add logging at each decision point
3. **Check if Combined packets starting with `00 03 5a` are being decompressed**
4. **Verify the decompressed data is parsed correctly as Combined subpackets**

## Config Files

- `/home/user/projects/claude/wimplo.json` - gfaydark character (FAILS)
- `/home/user/projects/claude/summonah.json` - qeynos2 character (WORKS)

## Important Reminders

- **DO NOT re-verify that the server sends the packet** - It does. Confirmed.
- **DO NOT run long tests** - 10-15 seconds is enough
- **ALWAYS redirect output to a file** - Don't pipe to grep, analyze the file separately
- **The zone server is at 172.18.0.3:7007** (docker container)
