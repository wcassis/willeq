Source: `/mnt/projects/fable/review/B909/before` (client), `/mnt/projects/fable/review/B909/bench` (benchmark). Client value = the LARGEST of the runs for that zone (one run per owner mode), with the spread across runs; benchmark value = the larger of the gate's golden and coverage invocations (`ledger` = `benchmark_end`). % = of the orangepi budget.


### dreadlands

Runs: spatial OK; zone_mesh OK; zone_parse OK

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,008 | 141,008 | 13.4% | 141,008 | 13.4% | 0 | 86,208 | 8.2% | 54,800 |
| `archives` | 4,194,304 | 2,158,240 | 2,158,240 | 51.5% | 2,158,240 | 51.5% | 128 | 1,324,160 | 31.6% | 834,080 |
| `zone_parse` | 33,554,432 | 4,419,024 | 4,419,024 | 13.2% | 4,419,024 | 13.2% | 4,624 | 3,388,704 | 10.1% | 1,030,320 |
| `zone_mesh` | 29,360,128 | 11,418,576 | 11,418,816 | 38.9% | 11,418,768 | 38.9% | 48 | 12,417,664 | 42.3% | -998,896 |
| `spatial` | 4,194,304 | 9,352,912 | 9,352,912 **OVER** | 223.0% | 9,352,912 **OVER** | 223.0% | 176 | 1,251,840 | 29.8% | 8,101,072 |
| `textures_decoded` | 20,971,520 | 940,448 | 940,448 | 4.5% | 940,448 | 4.5% | 256 | 159,552 | 0.8% | 780,896 |
| `entity_models` | 88,080,384 | 17,491,360 | 4,720,016 | 5.4% | 4,720,016 | 5.4% | 352 | 4,483,040 | 5.1% | 236,976 |
| `objects` | 41,943,040 | 5,372,880 | 5,250,448 | 12.5% | 5,250,448 | 12.5% | 240 | 4,779,344 | 11.4% | 471,104 |
| `audio` | 33,554,432 | 1,596,064 | 1,596,064 | 4.8% | 1,596,064 | 4.8% | 5,248 | 0 | 0.0% | 1,596,064 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 225,648,640 (215.2 MiB) | 219,082,752 | 219,070,464 | 225,648,640 | +151.2 MiB | under by 40.8 MiB | 170,084,224 | 166,344,416 |
| zone_mesh | 225,660,928 (215.2 MiB) | 219,045,888 | 219,037,696 | 225,660,928 | +151.2 MiB | under by 40.8 MiB | 170,083,856 | 165,950,528 |
| zone_parse | 225,624,064 (215.2 MiB) | 219,131,904 | 219,119,616 | 225,624,064 | +151.2 MiB | under by 40.8 MiB | 170,083,984 | 174,673,568 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 9,352,736 | 8,688 | 9,352,736 | 8,688 |
| startup.sp.region_tris | 5,183,312 | 4,569 | 5,183,312 | 4,569 |
| startup.sp.object_tris | 3,762,480 | 550 | 3,762,480 | 550 |
| startup.sp.region_bounds | 334,976 | 3,486 | 334,976 | 3,486 |
| startup.sp.object_regions | 57,376 | 1 | 57,376 | 1 |
| startup.misc | 13,536 | 81 | 13,536 | 81 |
| startup.sp.light_regions | 1,056 | 1 | 1,056 | 1 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_mesh`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 11,418,768 | 389 | 11,418,720 | 388 |
| startup.zm.build | 10,172,688 | 366 | 10,172,688 | 366 |
| startup.zm.occluders | 1,243,136 | 3 | 1,243,136 | 3 |
| startup.zm.load_thread_default | 2,752 | 19 | 2,704 | 18 |
| startup.zm.anim_textures | 192 | 1 | 192 | 1 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_parse`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 4,419,024 | 3,083 | 4,419,024 | 3,083 |
| startup.misc | 4,419,024 | 3,083 | 4,419,024 | 3,083 |

`zone_parse` call sites at steady_60s (`WILLEQ_LEDGER_TRACE=zone_parse:startup`, first non-allocator frame; traced bytes only):

| first frame | live | blocks |
|---|---:|---:|
| `EQT::Graphics::BspRegion::pvsMemoryBytes() const` | 1,599,248 | 1 |
| `EQT::Graphics::BspTree::BspTree()` | 989,536 | 11 |
| `EQT::Graphics::S3DLoader::loadObjectsFromArchive(EQT::Graphics::PfsArchive&, std::__cxx11::basic_string<char, ` | 704,176 | 409 |
| `EQT::Graphics::WldLoader::parseFragment21(char const*, unsigned int, unsigned int)` | 577,024 | 1 |
| `EQT::Graphics::WldLoader::parseFragment22(char const*, unsigned int, unsigned int, EQT::Graphics::BspRegionPoo` | 164,896 | 1 |
| `EQT::Graphics::WldLoader::parseFragment2D(char const*, unsigned int, unsigned int)` | 117,216 | 814 |
| `EQT::Graphics::WldLoader::parseFragment37(char const*, unsigned int, unsigned int, int, char const*)` | 87,920 | 73 |
| `EQT::Graphics::S3DLoader::loadCharacterTextures(EQT::Graphics::PfsArchive&)` | 17,280 | 120 |

### gfaydark

Runs: spatial OK; zone_mesh OK; zone_parse OK

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,008 | 141,008 | 13.4% | 141,008 | 13.4% | 0 | 86,208 | 8.2% | 54,800 |
| `archives` | 4,194,304 | 2,136,128 | 2,136,128 | 50.9% | 2,136,128 | 50.9% | 832 | 1,301,296 | 31.0% | 834,832 |
| `zone_parse` | 33,554,432 | 1,754,544 | 1,754,544 | 5.2% | 1,754,544 | 5.2% | 4,784 | 757,872 | 2.3% | 996,672 |
| `zone_mesh` | 29,360,128 | 6,537,840 | 6,522,960 | 22.2% | 6,522,192 | 22.2% | 16 | 6,781,824 | 23.1% | -259,632 |
| `spatial` | 4,194,304 | 8,491,552 | 8,491,552 **OVER** | 202.5% | 8,491,552 **OVER** | 202.5% | 64 | 621,328 | 14.8% | 7,870,224 |
| `textures_decoded` | 20,971,520 | 928,560 | 928,560 | 4.4% | 965,616 | 4.6% | 25,136 | 111,504 | 0.5% | 854,112 |
| `entity_models` | 88,080,384 | 16,744,544 | 9,736,544 | 11.1% | 11,416,640 | 13.0% | 1,031,264 | 4,225,472 | 4.8% | 7,191,168 |
| `objects` | 41,943,040 | 5,413,952 | 5,413,952 | 12.9% | 5,413,952 | 12.9% | 400 | 9,804,304 | 23.4% | -4,390,352 |
| `audio` | 33,554,432 | 1,597,936 | 1,597,936 | 4.8% | 1,597,936 | 4.8% | 71,792 | 0 | 0.0% | 1,597,936 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 374,767,616 (357.4 MiB) | 205,443,072 | 205,438,976 | 374,767,616 | +293.4 MiB | OVER by 101.4 MiB | 159,986,560 | 312,564,560 |
| zone_mesh | 255,328,256 (243.5 MiB) | 200,982,528 | 200,978,432 | 254,889,984 | +179.5 MiB | under by 12.5 MiB | 159,023,072 | 196,032,912 |
| zone_parse | 374,681,600 (357.3 MiB) | 200,192,000 | 200,187,904 | 373,575,680 | +293.3 MiB | OVER by 101.3 MiB | 159,258,528 | 325,525,568 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 8,491,520 | 5,684 | 8,491,520 | 5,684 |
| startup.sp.object_tris | 5,257,648 | 1,158 | 5,257,648 | 1,158 |
| startup.sp.region_tris | 2,840,736 | 2,889 | 2,840,736 | 2,889 |
| startup.sp.object_regions | 229,408 | 1 | 229,408 | 1 |
| startup.sp.region_bounds | 154,928 | 1,597 | 154,928 | 1,597 |
| startup.misc | 6,720 | 38 | 6,720 | 38 |
| startup.sp.light_regions | 2,080 | 1 | 2,080 | 1 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_mesh`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 6,522,960 | 190 | 6,522,192 | 185 |
| startup.zm.build | 5,907,376 | 167 | 5,907,376 | 167 |
| startup.zm.occluders | 612,608 | 3 | 612,608 | 3 |
| startup.zm.load_thread_default | 2,784 | 19 | 2,016 | 14 |
| startup.zm.anim_textures | 192 | 1 | 192 | 1 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_parse`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 1,754,544 | 4,746 | 1,754,544 | 4,746 |
| startup.misc | 1,754,544 | 4,746 | 1,754,544 | 4,746 |

`zone_parse` call sites at steady_60s (`WILLEQ_LEDGER_TRACE=zone_parse:startup`, first non-allocator frame; traced bytes only):

| first frame | live | blocks |
|---|---:|---:|
| `EQT::Graphics::S3DLoader::loadObjectsFromArchive(EQT::Graphics::PfsArchive&, std::__cxx11::basic_string<char, ` | 449,440 | 253 |
| `EQT::Graphics::WldLoader::parseFragment2D(char const*, unsigned int, unsigned int)` | 407,408 | 2,829 |
| `EQT::Graphics::BspTree::BspTree()` | 278,976 | 3 |
| `EQT::Graphics::BspRegion::pvsMemoryBytes() const` | 210,208 | 1 |
| `EQT::Graphics::WldLoader::parseFragment21(char const*, unsigned int, unsigned int)` | 162,688 | 1 |
| `EQT::Graphics::WldLoader::parseFragment22(char const*, unsigned int, unsigned int, EQT::Graphics::BspRegionPoo` | 46,512 | 1 |
| `EQT::Graphics::WldLoader::parseFragment37(char const*, unsigned int, unsigned int, int, char const*)` | 38,400 | 30 |
| `EQT::Graphics::WldLoader::parseFragment35(char const*, unsigned int, unsigned int)` | 24,688 | 140 |
| `EQT::Graphics::S3DLoader::loadCharacterTextures(EQT::Graphics::PfsArchive&)` | 18,720 | 130 |

### iceclad

Runs: spatial OK (zone link lost 2026-09-29 05:49:58.282); zone_mesh OK (zone link lost 2026-09-29 05:56:13.577); zone_parse OK (zone link lost 2026-09-29 06:02:31.283)

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,008 | 141,008 | 13.4% | 141,008 | 13.4% | 0 | 86,208 | 8.2% | 54,800 |
| `archives` | 4,194,304 | 2,125,984 | 2,125,984 | 50.7% | 2,125,984 | 50.7% | 496 | 1,290,688 | 30.8% | 835,296 |
| `zone_parse` | 33,554,432 | 11,714,736 | 11,714,736 | 34.9% | 11,714,736 | 34.9% | 4,720 | 11,329,968 | 33.8% | 384,768 |
| `zone_mesh` | 29,360,128 | 11,250,368 | 11,219,840 | 38.2% | 11,219,664 | 38.2% | 16 | 19,064,912 | 64.9% | -7,845,248 |
| `spatial` | 4,194,304 | 8,129,968 | 8,129,968 **OVER** | 193.8% | 8,129,968 **OVER** | 193.8% | 96 | 2,786,288 | 66.4% | 5,343,680 |
| `textures_decoded` | 20,971,520 | 794,416 | 794,416 | 3.8% | 824,352 | 3.9% | 4,816 | 83,584 | 0.4% | 740,768 |
| `entity_models` | 88,080,384 | 12,292,832 | 7,136,384 | 8.1% | 7,237,824 | 8.2% | 2,896 | 4,493,520 | 5.1% | 2,744,304 |
| `objects` | 41,943,040 | 427,952 | 427,952 | 1.0% | 427,952 | 1.0% | 80 | 488,032 | 1.2% | -60,080 |
| `audio` | 33,554,432 | 1,626,976 | 1,626,976 | 4.8% | 1,626,976 | 4.8% | 5,232 | 0 | 0.0% | 1,626,976 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 291,377,152 (277.9 MiB) | 291,377,152 | 286,679,040 | 288,321,536 | +213.9 MiB | OVER by 21.9 MiB | 205,762,688 | 208,051,360 |
| zone_mesh | 291,643,392 (278.1 MiB) | 291,643,392 | 286,806,016 | 289,067,008 | +214.1 MiB | OVER by 22.1 MiB | 205,761,488 | 204,114,016 |
| zone_parse | 285,614,080 (272.4 MiB) | 285,614,080 | 280,956,928 | 283,013,120 | +208.4 MiB | OVER by 16.4 MiB | 205,761,824 | 212,113,552 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 8,129,872 | 38,417 | 8,129,872 | 38,417 |
| startup.sp.region_tris | 6,068,560 | 20,424 | 6,068,560 | 20,424 |
| startup.sp.region_bounds | 1,717,184 | 17,886 | 1,717,184 | 17,886 |
| startup.sp.object_tris | 337,376 | 80 | 337,376 | 80 |
| startup.sp.object_regions | 3,616 | 1 | 3,616 | 1 |
| startup.misc | 2,848 | 25 | 2,848 | 25 |
| startup.sp.light_regions | 288 | 1 | 288 | 1 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_mesh`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 11,219,840 | 110 | 11,219,664 | 108 |
| startup.zm.build | 8,438,960 | 82 | 8,438,960 | 82 |
| startup.zm.occluders | 2,777,568 | 3 | 2,777,568 | 3 |
| startup.zm.load_thread_default | 2,496 | 18 | 2,320 | 16 |
| startup.zm.anim_textures | 816 | 7 | 816 | 7 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_parse`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 11,714,736 | 5,720 | 11,714,736 | 5,720 |
| startup.misc | 11,714,736 | 5,720 | 11,714,736 | 5,720 |

`zone_parse` call sites at steady_60s (`WILLEQ_LEDGER_TRACE=zone_parse:startup`, first non-allocator frame; traced bytes only):

| first frame | live | blocks |
|---|---:|---:|
| `EQT::Graphics::BspRegion::pvsMemoryBytes() const` | 4,832,336 | 1 |
| `EQT::Graphics::BspTree::BspTree()` | 3,611,648 | 37 |
| `EQT::Graphics::WldLoader::parseFragment21(char const*, unsigned int, unsigned int)` | 2,106,112 | 1 |
| `EQT::Graphics::WldLoader::parseFragment22(char const*, unsigned int, unsigned int, EQT::Graphics::BspRegionPoo` | 601,776 | 1 |
| `EQT::Graphics::S3DLoader::loadObjectsFromArchive(EQT::Graphics::PfsArchive&, std::__cxx11::basic_string<char, ` | 242,000 | 62 |
| `EQT::Graphics::WldLoader::parseFragment29(char const*, unsigned int, unsigned int, int, char const*)` | 150,752 | 4,711 |
| `EQT::Graphics::WldLoader::parseFragment37(char const*, unsigned int, unsigned int, int, char const*)` | 71,200 | 59 |

### timorous

Runs: spatial OK; zone_mesh OK; zone_parse OK (zone link lost 2026-09-29 06:12:37.187)

| category | budget | client zone_load_complete | client ready | % | client steady_60s | % | run spread (steady) | benchmark | % | client steady − bench |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `unattributed` | 1,048,576 | 141,008 | 141,008 | 13.4% | 141,008 | 13.4% | 0 | 86,208 | 8.2% | 54,800 |
| `archives` | 4,194,304 | 2,163,232 | 2,163,232 | 51.6% | 2,163,232 | 51.6% | 8,080 | 1,319,360 | 31.5% | 843,872 |
| `zone_parse` | 33,554,432 | 7,163,936 | 7,163,936 | 21.4% | 7,163,936 | 21.4% | 4,656 | 5,529,168 | 16.5% | 1,634,768 |
| `zone_mesh` | 29,360,128 | 17,503,184 | 17,503,792 | 59.6% | 17,503,152 | 59.6% | 96 | 23,587,088 | 80.3% | -6,083,936 |
| `spatial` | 4,194,304 | 13,762,720 | 13,762,720 **OVER** | 328.1% | 13,762,720 **OVER** | 328.1% | 800 | 3,019,840 | 72.0% | 10,742,880 |
| `textures_decoded` | 20,971,520 | 888,592 | 888,592 | 4.2% | 1,050,880 | 5.0% | 720 | 191,264 | 0.9% | 859,616 |
| `entity_models` | 88,080,384 | 17,346,560 | 2,922,816 | 3.3% | 12,861,760 | 14.6% | 472,016 | 4,252,464 | 4.8% | 8,609,296 |
| `objects` | 41,943,040 | 4,822,736 | 4,761,664 | 11.4% | 4,761,664 | 11.4% | 336 | 4,924,800 | 11.7% | -163,136 |
| `audio` | 33,554,432 | 1,598,256 | 1,598,256 | 4.8% | 1,593,040 | 4.7% | 80 | 0 | 0.0% | 1,593,040 |

| run | peak RssAnon (2 s samples + points) | zone_load_complete | ready | steady_60s | peak vs 64 MB | peak vs 256 MB | gpu_driver ready | gpu_driver steady_60s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| spatial | 239,689,728 (228.6 MiB) | 231,669,760 | 231,661,568 | 239,689,728 | +164.6 MiB | under by 27.4 MiB | 140,122,608 | 168,891,760 |
| zone_mesh | 283,136,000 (270.0 MiB) | 280,440,832 | 280,436,736 | 283,136,000 | +206.0 MiB | OVER by 14.0 MiB | 191,416,208 | 209,821,024 |
| zone_parse | 281,436,160 (268.4 MiB) | 244,428,800 | 244,420,608 | 281,436,160 | +204.4 MiB | OVER by 12.4 MiB | 153,764,896 | 207,659,792 |

Owner rows, `WILLEQ_LEDGER_OWNERS=spatial`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 13,762,720 | 31,369 | 13,762,720 | 31,369 |
| startup.sp.region_tris | 8,674,160 | 17,646 | 8,674,160 | 17,646 |
| startup.sp.object_tris | 3,756,192 | 492 | 3,756,192 | 492 |
| startup.sp.region_bounds | 1,267,888 | 13,189 | 1,267,888 | 13,189 |
| startup.sp.object_regions | 57,376 | 1 | 57,376 | 1 |
| startup.misc | 7,104 | 41 | 7,104 | 41 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_mesh`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 17,503,760 | 220 | 17,503,152 | 216 |
| startup.zm.build | 14,488,384 | 186 | 14,488,384 | 186 |
| startup.zm.occluders | 3,011,136 | 3 | 3,011,136 | 3 |
| startup.zm.load_thread_default | 2,784 | 18 | 2,176 | 14 |
| startup.zm.anim_textures | 1,456 | 13 | 1,456 | 13 |

Owner rows, `WILLEQ_LEDGER_OWNERS=zone_parse`:

| owner | ready live | blocks | steady_60s live | blocks |
|---|---:|---:|---:|---:|
| TOTAL | 7,163,936 | 6,811 | 7,163,936 | 6,811 |
| startup.misc | 7,163,936 | 6,811 | 7,163,936 | 6,811 |

`zone_parse` call sites at steady_60s (`WILLEQ_LEDGER_TRACE=zone_parse:startup`, first non-allocator frame; traced bytes only):

| first frame | live | blocks |
|---|---:|---:|
| `EQT::Graphics::BspRegion::pvsMemoryBytes() const` | 2,130,288 | 1 |
| `EQT::Graphics::BspTree::BspTree()` | 1,855,424 | 19 |
| `EQT::Graphics::S3DLoader::loadObjectsFromArchive(EQT::Graphics::PfsArchive&, std::__cxx11::basic_string<char, ` | 1,128,624 | 418 |
| `EQT::Graphics::WldLoader::parseFragment21(char const*, unsigned int, unsigned int)` | 1,081,984 | 1 |
| `EQT::Graphics::WldLoader::parseFragment22(char const*, unsigned int, unsigned int, EQT::Graphics::BspRegionPoo` | 309,168 | 1 |
| `EQT::Graphics::WldLoader::parseFragment37(char const*, unsigned int, unsigned int, int, char const*)` | 250,784 | 104 |
| `EQT::Graphics::WldLoader::parseFragment29(char const*, unsigned int, unsigned int, int, char const*)` | 121,600 | 3,800 |
| `EQT::Graphics::WldLoader::parseFragment2D(char const*, unsigned int, unsigned int)` | 93,744 | 651 |
| `EQT::Graphics::WldLoader::parseFragment36(char const*, unsigned int, unsigned int, int, char const*, bool)` | 53,312 | 442 |
| `EQT::Graphics::S3DLoader::loadCharacterTextures(EQT::Graphics::PfsArchive&)` | 52,736 | 412 |
