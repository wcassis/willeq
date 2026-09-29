Source: `/mnt/projects/fable/review/B909/pi-ac`. Frame time = median of the client's `/frametiming` 60-frame blocks ("Total Frame", us); moving = wholly inside the key sequence, idle = after it. fps = median of the blocks' fps estimates.

| zone | side | runs ok | blocks mov/idle | moving frame us | moving fps | Player Movement us (mov / idle) | idle frame us | idle fps | spatial @ready | spatial @steady | peak RssAnon | RssAnon @steady |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| gfaydark | A | 3/3 | 50/23 | 33,301 | 30.0 | 392 / 192 | 33,313 | 30.0 | 8,273,728–8,274,152 | 8,273,696–8,274,120 | 102,572,032–103,485,440 | 97,034,240–98,332,672 |
| gfaydark | B | 3/3 | 48/24 | 33,304 | 30.0 | 435 / 191 | 33,313 | 30.0 | 5,519,064–5,519,240 | 5,519,032–5,519,208 | 99,450,880–100,900,864 | 94,363,648–98,390,016 |
| growthplane | A | 3/3 | 41/23 | 37,116 | 26.9 | 417 / 165 | 35,764 | 28.0 | 44,097,376–44,097,424 | 44,097,344–44,097,392 | 176,017,408–176,156,672 | 168,636,416–168,775,680 |
| growthplane | B | 3/3 | 41/22 | 37,943 | 26.4 | 476 / 165 | 36,988 | 27.1 | 40,709,192–40,709,216 | 40,709,160–40,709,184 | 172,666,880–172,834,816 | 163,631,104–165,453,824 |
| iceclad | A | 3/3 | 60/23 | 26,523 | 37.7 | 3,012 / 3,032 | 26,328 | 38.0 | 7,093,928–7,094,000 | 7,093,928–7,094,000 | 136,003,584–136,089,600 | 129,150,976–129,167,360 |
| iceclad | B | 3/3 | 49/9 | 33,318 | 30.0 | 3,067 / 1,298 | 65,434 | 15.3 | 1,533,840–1,533,944 | 1,533,840–1,533,944 | 130,469,888–135,143,424 | 112,320,512–124,108,800 |
| timorous | A | 3/3 | 41/18 | 38,010 | 26.3 | 5,016 / 1,994 | 35,797 | 27.9 | 12,906,968–12,906,984 | 12,906,936–12,906,976 | 163,004,416–165,830,656 | 150,933,504–158,068,736 |
| timorous | B | 3/3 | 43/22 | 35,888 | 27.9 | 5,346 / 5,346 | 35,526 | 28.1 | 4,673,984–4,674,008 | 4,673,968–4,673,984 | 161,116,160–165,400,576 | 145,514,496–157,421,568 |

- gfaydark: B vs A median moving frame time +0.01 % (A 33,301 us / 30.0 fps, B 33,304 us / 30.0 fps); Player Movement A 392 us, B 435 us (+0.13 % of A's frame)
- growthplane: B vs A median moving frame time +2.23 % (A 37,116 us / 26.9 fps, B 37,943 us / 26.4 fps); Player Movement A 417 us, B 476 us (+0.16 % of A's frame)
- iceclad: B vs A median moving frame time +25.62 % (A 26,523 us / 37.7 fps, B 33,318 us / 30.0 fps); Player Movement A 3,012 us, B 3,067 us (+0.21 % of A's frame) -- B SLOWER THAN A BY >3 %
- timorous: B vs A median moving frame time -5.58 % (A 38,010 us / 26.3 fps, B 35,888 us / 27.9 fps); Player Movement A 5,016 us, B 5,346 us (+0.87 % of A's frame)

spatial owner rows at steady_60s (per leg):

| zone | leg | owner rows |
|---|---|---|
| gfaydark | r1_A | misc 8,274,016 |
| gfaydark | r2_A | misc 8,274,120 |
| gfaydark | r3_A | misc 8,273,696 |
| gfaydark | r1_B | sp.object_tris 5,230,240, sp.object_regions 163,864, sp.region_bounds 102,784, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r2_B | sp.object_tris 5,230,072, sp.object_regions 163,864, sp.region_bounds 102,768, sp.region_tris 16,536, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r3_B | sp.object_tris 5,230,112, sp.object_regions 163,864, sp.region_bounds 102,800, sp.region_tris 16,536, misc 4,744, sp.light_regions 1,048 |
| growthplane | r1_A | misc 44,097,344 |
| growthplane | r2_A | misc 44,097,392 |
| growthplane | r3_A | misc 44,097,384 |
| growthplane | r1_B | sp.object_tris 40,188,128, sp.object_regions 327,704, sp.region_bounds 175,888, sp.region_tris 11,728, misc 5,456, sp.light_regions 280 |
| growthplane | r2_B | sp.object_tris 40,188,096, sp.object_regions 327,704, sp.region_bounds 175,904, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| growthplane | r3_B | sp.object_tris 40,188,104, sp.object_regions 327,704, sp.region_bounds 175,896, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| iceclad | r1_A | misc 7,093,928 |
| iceclad | r2_A | misc 7,094,000 |
| iceclad | r3_A | misc 7,093,976 |
| iceclad | r1_B | sp.region_bounds 1,144,824, sp.object_tris 335,488, sp.region_tris 48,752, sp.object_regions 2,584, misc 2,040, sp.light_regions 152 |
| iceclad | r2_B | sp.region_bounds 1,144,808, sp.object_tris 335,480, sp.region_tris 48,880, sp.object_regions 2,584, misc 2,040, sp.light_regions 152 |
| iceclad | r3_B | sp.region_bounds 1,144,808, sp.object_tris 335,480, sp.region_tris 48,872, sp.object_regions 2,584, misc 2,040, sp.light_regions 152 |
| timorous | r1_A | misc 12,906,976 |
| timorous | r2_A | misc 12,906,936 |
| timorous | r3_A | misc 12,906,952 |
| timorous | r1_B | sp.object_tris 3,744,800, sp.region_bounds 844,744, sp.object_regions 40,984, sp.region_tris 38,424, misc 5,032 |
| timorous | r2_B | sp.object_tris 3,744,784, sp.region_bounds 844,744, sp.object_regions 40,984, sp.region_tris 38,424, misc 5,032 |
| timorous | r3_B | sp.object_tris 3,744,792, sp.region_bounds 844,744, sp.object_regions 40,984, sp.region_tris 38,424, misc 5,032 |
