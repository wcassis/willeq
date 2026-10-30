# Official-client oracle session

Every count below is derived from a cited step file, not from visual inference.

| Step | Action | Still | Video frame/contact | Decoded packets | INI diff |
| --- | --- | --- | --- | --- | --- |
| 01_char_select | login Testcaster | [01_char_select.png](01_char_select.png) | [01_char_select.frame.png](01_char_select.frame.png) | [01_char_select.opcodes.tsv](01_char_select.opcodes.tsv): 25 data rows | [01_char_select.ini.diff](01_char_select.ini.diff) |
| 02_in_world | zone qeynos2 | [02_in_world.png](02_in_world.png) | [02_in_world.frame.png](02_in_world.frame.png) | [02_in_world.opcodes.tsv](02_in_world.opcodes.tsv): 1137 data rows | [02_in_world.ini.diff](02_in_world.ini.diff) |
| 03_settle | wait 9000 | [03_settle.png](03_settle.png) | [03_settle.frame.png](03_settle.frame.png) | [03_settle.opcodes.tsv](03_settle.opcodes.tsv): 199 data rows | [03_settle.ini.diff](03_settle.ini.diff) |
| 04_close_tip | click 1229,555 | [04_close_tip.png](04_close_tip.png) | [04_close_tip.frame.png](04_close_tip.frame.png) | [04_close_tip.opcodes.tsv](04_close_tip.opcodes.tsv): 96 data rows | [04_close_tip.ini.diff](04_close_tip.ini.diff) |
| 05_goto_up | command #goto -74 428 10 | [05_goto_up.png](05_goto_up.png) | [05_goto_up.frame.png](05_goto_up.frame.png) | [05_goto_up.opcodes.tsv](05_goto_up.opcodes.tsv): 91 data rows | [05_goto_up.ini.diff](05_goto_up.ini.diff) |
| 06_goto_settle | wait 6000 | [06_goto_settle.png](06_goto_settle.png) | [06_goto_settle.frame.png](06_goto_settle.frame.png) | [06_goto_settle.opcodes.tsv](06_goto_settle.opcodes.tsv): 320 data rows | [06_goto_settle.ini.diff](06_goto_settle.ini.diff) |
| 07_level | command #level 60 | [07_level.png](07_level.png) | [07_level.frame.png](07_level.frame.png) | [07_level.opcodes.tsv](07_level.opcodes.tsv): 127 data rows | [07_level.ini.diff](07_level.ini.diff) |
| 08_level_settle | wait 2000 | [08_level_settle.png](08_level_settle.png) | [08_level_settle.frame.png](08_level_settle.frame.png) | [08_level_settle.opcodes.tsv](08_level_settle.opcodes.tsv): 162 data rows | [08_level_settle.ini.diff](08_level_settle.ini.diff) |
| 09_gdb_arm | wait 3000 | [09_gdb_arm.png](09_gdb_arm.png) | [09_gdb_arm.frame.png](09_gdb_arm.frame.png) | [09_gdb_arm.opcodes.tsv](09_gdb_arm.opcodes.tsv): 214 data rows | [09_gdb_arm.ini.diff](09_gdb_arm.ini.diff) |
| 10_gdb_dump | wait 10000 | [10_gdb_dump.png](10_gdb_dump.png) | [10_gdb_dump.frame.png](10_gdb_dump.frame.png) | [10_gdb_dump.opcodes.tsv](10_gdb_dump.opcodes.tsv): 460 data rows | [10_gdb_dump.ini.diff](10_gdb_dump.ini.diff) |
| 11_stand0 | still 0 | [11_stand0.png](11_stand0.png) | [11_stand0.frame.png](11_stand0.frame.png) | [11_stand0.opcodes.tsv](11_stand0.opcodes.tsv): 90 data rows | [11_stand0.ini.diff](11_stand0.ini.diff) |
| 12_stun_start | command #stun 8000 | [12_stun_start.png](12_stun_start.png) | [12_stun_start.frame.png](12_stun_start.frame.png) | [12_stun_start.opcodes.tsv](12_stun_start.opcodes.tsv): 113 data rows | [12_stun_start.ini.diff](12_stun_start.ini.diff) |
| 13_stunned | wait 2500 | [13_stunned.png](13_stunned.png) | [13_stunned.frame.png](13_stunned.frame.png) | [13_stunned.opcodes.tsv](13_stunned.opcodes.tsv): 242 data rows | [13_stunned.ini.diff](13_stunned.ini.diff) |
| 14_stun_end | wait 7000 | [14_stun_end.png](14_stun_end.png) | [14_stun_end.frame.png](14_stun_end.frame.png) | [14_stun_end.opcodes.tsv](14_stun_end.opcodes.tsv): 447 data rows | [14_stun_end.ini.diff](14_stun_end.ini.diff) |
| 15_post_stun | wait 3000 | [15_post_stun.png](15_post_stun.png) | [15_post_stun.frame.png](15_post_stun.frame.png) | [15_post_stun.opcodes.tsv](15_post_stun.opcodes.tsv): 214 data rows | [15_post_stun.ini.diff](15_post_stun.ini.diff) |
| 16_quit | command /q | [16_quit.png](16_quit.png) | [16_quit.frame.png](16_quit.frame.png) | [16_quit.opcodes.tsv](16_quit.opcodes.tsv): 15 data rows | [16_quit.ini.diff](16_quit.ini.diff) |

[Video step analysis](step_frames.tsv) uses the existing step_frames.py.

[All-step contact sheet](contact.png)

Raw capture: [capture.pcap](capture.pcap); video: [video.mp4](video.mp4).
Packet totals and per-step timestamps: [steps.tsv](steps.tsv).
