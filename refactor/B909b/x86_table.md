| zone | side | spatial @ready | spatial @steady_60s | % of 4 MiB | objects @steady | RssAnon @steady | gpu_driver @steady | RssAnon − gpu_driver | over 64 MiB by |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| gfaydark | before | 5,674,432 | 5,674,432 | 135.3 % | 5,413,648 | 258,514,944 | 222,958,336 | 35,556,608 | -31,552,256 |
| gfaydark | after | 740,016 | 740,016 | 17.6 % | 5,413,600 | 252,096,512 | 197,486,688 | 54,609,824 | -12,499,040 |
| growthplane | before | 40,962,176 | 40,962,176 | 976.6 % | 47,107,104 | 340,197,376 | 240,773,504 | 99,423,872 | 32,315,008 |
| growthplane | after | 1,236,416 | 1,236,416 | 29.5 % | 47,106,960 | 234,528,768 | 156,325,552 | 78,203,216 | 11,094,352 |
| overthere | before | 16,063,168 | 16,063,168 | 383.0 % | 21,891,136 | 293,830,656 | 193,920,368 | 99,910,288 | 32,801,424 |
| overthere | after | 1,106,400 | 1,106,400 | 26.4 % | 21,888,464 | 249,282,560 | 150,448,256 | 98,834,304 | 31,725,440 |
| trakanon | before | 26,014,016 | 26,014,016 | 620.2 % | 27,511,216 | 248,242,176 | 142,460,880 | 105,781,296 | 38,672,432 |
| trakanon | after | 929,376 | 929,376 | 22.2 % | 27,512,000 | 225,656,832 | 164,796,880 | 60,859,952 | -6,248,912 |

| zone | side | owner | @ready | @steady_60s |
|---|---|---|---:|---:|
| gfaydark | before | `sp.object_tris` | 5,257,648 | 5,257,648 |
| gfaydark | before | `sp.object_regions` | 229,408 | 229,408 |
| gfaydark | before | `sp.region_bounds` | 155,008 | 155,008 |
| gfaydark | before | `sp.region_tris` | 23,568 | 23,568 |
| gfaydark | before | `sp.light_regions` | 2,080 | 2,080 |
| gfaydark | after | `sp.object_instances` | 323,232 | 323,232 |
| gfaydark | after | `sp.object_regions` | 229,408 | 229,408 |
| gfaydark | after | `sp.region_bounds` | 155,008 | 155,008 |
| gfaydark | after | `sp.region_tris` | 23,568 | 23,568 |
| gfaydark | after | `sp.light_regions` | 2,080 | 2,080 |
| growthplane | before | `sp.object_tris` | 40,212,336 | 40,212,336 |
| growthplane | before | `sp.object_regions` | 458,784 | 458,784 |
| growthplane | before | `sp.region_bounds` | 263,264 | 263,264 |
| growthplane | before | `sp.region_tris` | 19,632 | 19,632 |
| growthplane | before | `sp.light_regions` | 544 | 544 |
| growthplane | after | `sp.object_instances` | 486,576 | 486,576 |
| growthplane | after | `sp.object_regions` | 458,784 | 458,784 |
| growthplane | after | `sp.region_bounds` | 263,264 | 263,264 |
| growthplane | after | `sp.region_tris` | 19,632 | 19,632 |
| growthplane | after | `sp.light_regions` | 544 | 544 |
| overthere | before | `sp.object_tris` | 15,302,960 | 15,302,960 |
| overthere | before | `sp.region_bounds` | 481,024 | 481,024 |
| overthere | before | `sp.object_regions` | 229,408 | 229,408 |
| overthere | before | `sp.region_tris` | 32,800 | 32,800 |
| overthere | before | `sp.light_regions` | 1,056 | 1,056 |
| overthere | after | `sp.region_bounds` | 479,184 | 479,184 |
| overthere | after | `sp.object_instances` | 348,032 | 348,032 |
| overthere | after | `sp.object_regions` | 229,408 | 229,408 |
| overthere | after | `sp.region_tris` | 32,800 | 32,800 |
| overthere | after | `sp.light_regions` | 1,056 | 1,056 |
| trakanon | before | `sp.object_tris` | 25,431,600 | 25,431,600 |
| trakanon | before | `sp.region_bounds` | 317,984 | 317,984 |
| trakanon | before | `sp.object_regions` | 229,408 | 229,408 |
| trakanon | before | `sp.region_tris` | 24,848 | 24,848 |
| trakanon | before | `sp.light_regions` | 2,080 | 2,080 |
| trakanon | after | `sp.object_instances` | 346,880 | 346,880 |
| trakanon | after | `sp.region_bounds` | 318,064 | 318,064 |
| trakanon | after | `sp.object_regions` | 229,408 | 229,408 |
| trakanon | after | `sp.region_tris` | 24,848 | 24,848 |
| trakanon | after | `sp.light_regions` | 2,080 | 2,080 |

- gfaydark before: ('old', 2829, 111012, 579)
- gfaydark after: ('new', 2829, 111012, 579, 79, 3988, 323048, 257412, 3996432)
  - sharing: 2829 instances over 80 distinct meshes (max 1247 instances of one mesh); largest per-instance-copy contributors: NEKPINE1 x1247 (36 tris), NEKPINE2 x1068 (36 tris), FELCHAIR x58 (86 tris), TORCH1 x65 (57 tris), KELBRAZ x39 (48 tris)
- growthplane before: ('old', 4333, 814110, 702)
- growthplane after: ('new', 4333, 814110, 702, 11, 1944, 486336, 88548, 29307960)
  - sharing: 4333 instances over 4321 distinct meshes (max 6 instances of one mesh); largest per-instance-copy contributors: POGTEMP200 x2 (320 tris), MUSHROOM3 x4 (142 tris), MUSHROOM4 x4 (122 tris), POGMONU200 x1 (348 tris), MUSHROOM2 x6 (54 tris)
- overthere before: ('old', 2879, 320954, 585)
- overthere after: ('new', 2879, 320954, 585, 62, 18363, 347696, 945690, 11554344)
  - sharing: 2879 instances over 2542 distinct meshes (max 61 instances of one mesh); largest per-instance-copy contributors: KUSTAT2P4 x9 (832 tris), KUSTAT2P3 x8 (832 tris), OVERROCK101 x42 (120 tris), OVERROCK103 x61 (80 tris), OTSHIP101 x2 (1568 tris)
- trakanon before: ('old', 2758, 522449, 792)
- trakanon after: ('new', 2758, 522449, 792, 57, 16719, 346592, 885054, 18808164)
  - sharing: 2758 instances over 2420 distinct meshes (max 44 instances of one mesh); largest per-instance-copy contributors: TRTSTONE101 x44 (386 tris), KURH400BJD x18 (622 tris), KURH100BJ x21 (332 tris), KUSTAT2P2 x8 (832 tris), KURH600BRJ x16 (373 tris)
