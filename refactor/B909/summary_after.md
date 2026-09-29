Source: `/mnt/projects/fable/review/B909/after` (client), `/mnt/projects/fable/review/B909/bench` (benchmark). Client value = the LARGEST of the runs for that zone (one run per owner mode), with the spread across runs; benchmark value = the larger of the gate's golden and coverage invocations (`ledger` = `benchmark_end`). % = of the orangepi budget.


### dreadlands

Runs: spatial OK

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,056 | 141,056 | 13.5% | 141,056 | 13.5% | 0 | 86,208 | 8.2% | 54,848 |
| `archives` | 4,194,304 | 2,158,160 | 2,158,160 | 51.5% | 2,158,160 | 51.5% | 0 | 1,324,160 | 31.6% | 834,000 |
| `zone_parse` | 33,554,432 | 4,414,320 | 4,414,320 | 13.2% | 4,414,320 | 13.2% | 0 | 3,388,704 | 10.1% | 1,025,616 |
| `zone_mesh` | 29,360,128 | 11,418,544 | 11,418,784 | 38.9% | 11,418,736 | 38.9% | 0 | 12,417,664 | 42.3% | -998,928 |
| `spatial` | 4,194,304 | 4,194,576 | 4,194,576 **OVER** | 100.0% | 4,194,576 **OVER** | 100.0% | 0 | 1,251,840 | 29.8% | 2,942,736 |
| `textures_decoded` | 20,971,520 | 940,400 | 940,400 | 4.5% | 940,400 | 4.5% | 0 | 159,552 | 0.8% | 780,848 |
| `entity_models` | 88,080,384 | 17,491,440 | 4,719,744 | 5.4% | 4,719,744 | 5.4% | 0 | 4,483,040 | 5.1% | 236,704 |
| `objects` | 41,943,040 | 5,373,024 | 5,250,272 | 12.5% | 5,250,272 | 12.5% | 0 | 4,779,344 | 11.4% | 470,928 |
| `audio` | 33,554,432 | 1,590,816 | 1,590,816 | 4.7% | 1,590,816 | 4.7% | 0 | 0 | 0.0% | 1,590,816 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 220,823,552 (210.6 MiB) | 214,171,648 | 214,171,648 | 220,823,552 | +146.6 MiB | under by 45.4 MiB | 170,084,064 | 148,964,448 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 4,194,576 | 4,131 | 4,194,576 | 4,131 |
| startup.sp.object_tris | 3,762,512 | 550 | 3,762,512 | 550 |
| startup.sp.region_bounds | 334,992 | 3,486 | 334,992 | 3,486 |
| startup.sp.object_regions | 57,376 | 1 | 57,376 | 1 |
| startup.sp.region_tris | 25,104 | 12 | 25,104 | 12 |
| startup.misc | 13,536 | 81 | 13,536 | 81 |
| startup.sp.light_regions | 1,056 | 1 | 1,056 | 1 |

### gfaydark

Runs: spatial OK

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,056 | 141,056 | 13.5% | 141,056 | 13.5% | 0 | 86,208 | 8.2% | 54,848 |
| `archives` | 4,194,304 | 2,136,096 | 2,136,096 | 50.9% | 2,136,096 | 50.9% | 0 | 1,301,296 | 31.0% | 834,800 |
| `zone_parse` | 33,554,432 | 1,749,840 | 1,749,840 | 5.2% | 1,749,840 | 5.2% | 0 | 757,872 | 2.3% | 991,968 |
| `zone_mesh` | 29,360,128 | 6,522,528 | 6,522,944 | 22.2% | 6,522,192 | 22.2% | 0 | 6,781,824 | 23.1% | -259,632 |
| `spatial` | 4,194,304 | 5,674,240 | 5,674,240 **OVER** | 135.3% | 5,674,240 **OVER** | 135.3% | 0 | 621,328 | 14.8% | 5,052,912 |
| `textures_decoded` | 20,971,520 | 909,776 | 909,776 | 4.3% | 927,440 | 4.4% | 0 | 111,504 | 0.5% | 815,936 |
| `entity_models` | 88,080,384 | 16,369,856 | 9,621,392 | 10.9% | 11,294,528 | 12.8% | 0 | 4,225,472 | 4.8% | 7,069,056 |
| `objects` | 41,943,040 | 5,429,488 | 5,414,096 | 12.9% | 5,414,096 | 12.9% | 0 | 9,804,304 | 23.4% | -4,390,208 |
| `audio` | 33,554,432 | 1,531,312 | 1,531,312 | 4.6% | 1,531,312 | 4.6% | 0 | 0 | 0.0% | 1,531,312 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 250,527,744 (238.9 MiB) | 203,345,920 | 203,341,824 | 250,527,744 | +174.9 MiB | under by 17.1 MiB | 159,978,624 | 203,018,640 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 5,674,240 | 2,827 | 5,674,240 | 2,827 |
| startup.sp.object_tris | 5,257,616 | 1,158 | 5,257,616 | 1,158 |
| startup.sp.object_regions | 229,408 | 1 | 229,408 | 1 |
| startup.sp.region_bounds | 154,848 | 1,597 | 154,848 | 1,597 |
| startup.sp.region_tris | 23,568 | 32 | 23,568 | 32 |
| startup.misc | 6,720 | 38 | 6,720 | 38 |
| startup.sp.light_regions | 2,080 | 1 | 2,080 | 1 |

### iceclad

Runs: spatial OK (zone link lost 2026-09-29 06:24:46.639)

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,056 | 141,056 | 13.5% | 141,056 | 13.5% | 0 | 86,208 | 8.2% | 54,848 |
| `archives` | 4,194,304 | 2,125,648 | 2,125,648 | 50.7% | 2,125,648 | 50.7% | 0 | 1,290,688 | 30.8% | 834,960 |
| `zone_parse` | 33,554,432 | 11,710,064 | 11,710,064 | 34.9% | 11,710,064 | 34.9% | 0 | 11,329,968 | 33.8% | 380,096 |
| `zone_mesh` | 29,360,128 | 11,250,320 | 11,219,840 | 38.2% | 11,219,664 | 38.2% | 0 | 19,064,912 | 64.9% | -7,845,248 |
| `spatial` | 4,194,304 | 2,152,000 | 2,152,000 | 51.3% | 2,152,000 | 51.3% | 0 | 2,786,288 | 66.4% | -634,288 |
| `textures_decoded` | 20,971,520 | 794,256 | 794,256 | 3.8% | 824,192 | 3.9% | 0 | 83,584 | 0.4% | 740,608 |
| `entity_models` | 88,080,384 | 12,290,528 | 7,133,520 | 8.1% | 7,235,824 | 8.2% | 0 | 4,493,520 | 5.1% | 2,742,304 |
| `objects` | 41,943,040 | 427,888 | 427,888 | 1.0% | 427,888 | 1.0% | 0 | 488,032 | 1.2% | -60,144 |
| `audio` | 33,554,432 | 1,626,944 | 1,626,944 | 4.8% | 1,626,944 | 4.8% | 0 | 0 | 0.0% | 1,626,944 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 250,118,144 (238.5 MiB) | 250,118,144 | 245,444,608 | 248,123,392 | +174.5 MiB | under by 17.5 MiB | 168,110,544 | 171,971,856 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 2,152,000 | 18,014 | 2,152,000 | 18,014 |
| startup.sp.region_bounds | 1,717,200 | 17,886 | 1,717,200 | 17,886 |
| startup.sp.object_tris | 337,376 | 80 | 337,376 | 80 |
| startup.sp.region_tris | 90,672 | 21 | 90,672 | 21 |
| startup.sp.object_regions | 3,616 | 1 | 3,616 | 1 |
| startup.misc | 2,848 | 25 | 2,848 | 25 |
| startup.sp.light_regions | 288 | 1 | 288 | 1 |

### timorous

Runs: spatial OK

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,056 | 141,056 | 13.5% | 141,056 | 13.5% | 0 | 86,208 | 8.2% | 54,848 |
| `archives` | 4,194,304 | 2,163,296 | 2,163,296 | 51.6% | 2,163,296 | 51.6% | 0 | 1,319,360 | 31.5% | 843,936 |
| `zone_parse` | 33,554,432 | 7,159,280 | 7,159,280 | 21.3% | 7,159,280 | 21.3% | 0 | 5,529,168 | 16.5% | 1,630,112 |
| `zone_mesh` | 29,360,128 | 17,503,184 | 17,503,808 | 59.6% | 17,503,072 | 59.6% | 0 | 23,587,088 | 80.3% | -6,084,016 |
| `spatial` | 4,194,304 | 5,162,576 | 5,162,576 **OVER** | 123.1% | 5,162,576 **OVER** | 123.1% | 0 | 3,019,840 | 72.0% | 2,142,736 |
| `textures_decoded` | 20,971,520 | 887,920 | 887,920 | 4.2% | 1,056,288 | 5.0% | 0 | 191,264 | 0.9% | 865,024 |
| `entity_models` | 88,080,384 | 17,346,656 | 2,922,672 | 3.3% | 12,846,960 | 14.6% | 0 | 4,252,464 | 4.8% | 8,594,496 |
| `objects` | 41,943,040 | 4,792,192 | 4,761,568 | 11.4% | 4,761,568 | 11.4% | 0 | 4,924,800 | 11.7% | -163,232 |
| `audio` | 33,554,432 | 1,592,976 | 1,592,976 | 4.7% | 1,592,976 | 4.7% | 0 | 0 | 0.0% | 1,592,976 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 347,316,224 (331.2 MiB) | 271,384,576 | 271,376,384 | 346,435,584 | +267.2 MiB | OVER by 75.2 MiB | 191,415,328 | 258,116,112 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 5,162,576 | 13,740 | 5,162,576 | 13,740 |
| startup.sp.object_tris | 3,756,192 | 492 | 3,756,192 | 492 |
| startup.sp.region_bounds | 1,267,248 | 13,189 | 1,267,248 | 13,189 |
| startup.sp.region_tris | 74,656 | 17 | 74,656 | 17 |
| startup.sp.object_regions | 57,376 | 1 | 57,376 | 1 |
| startup.misc | 7,104 | 41 | 7,104 | 41 |
