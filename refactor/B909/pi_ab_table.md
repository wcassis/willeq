Source: `/mnt/projects/fable/review/B909/pi-ab`. Frame time = median of the client's `/frametiming` 60-frame blocks ("Total Frame", us); moving = wholly inside the key sequence, idle = after it. fps = median of the blocks' fps estimates.

| zone | side | runs ok | blocks mov/idle | moving frame us | moving fps | Player Movement us (mov / idle) | idle frame us | idle fps | spatial @ready | spatial @steady | peak RssAnon | RssAnon @steady |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| gfaydark | A | 3/3 | 47/23 | 33,301 | 30.0 | 393 / 200 | 33,312 | 30.0 | 8,273,760–8,273,912 | 8,273,728–8,273,880 | 101,765,120–105,242,624 | 96,587,776–97,173,504 |
| gfaydark | B | 3/3 | 48/22 | 33,298 | 30.0 | 438 / 189 | 33,311 | 30.0 | 5,519,104–5,519,264 | 5,519,072–5,519,232 | 100,315,136–101,736,448 | 94,629,888–99,155,968 |
| growthplane | A | 3/3 | 40/22 | 37,120 | 26.9 | 400 / 162 | 35,930 | 27.9 | 44,097,360–44,097,512 | 44,097,328–44,097,480 | 176,087,040–176,340,992 | 166,866,944–168,800,256 |
| growthplane | B | 3/3 | 38/20 | 38,518 | 26.0 | 524 / 156 | 37,533 | 26.6 | 40,709,144–40,709,192 | 40,709,112–40,709,160 | 172,732,416–172,818,432 | 165,351,424–165,437,440 |
| iceclad | A | 3/3 | 49/20 | 33,318 | 30.0 | 3,023 / 1,396 | 33,320 | 30.0 | 7,093,888–7,093,976 | 7,093,888–7,093,944 | 132,935,680–137,859,072 | 119,111,680–130,387,968 |
| iceclad | B | 3/3 | 60/24 | 26,352 | 38.0 | 3,067 / 3,080 | 26,298 | 38.0 | 1,533,864–1,534,000 | 1,533,832–1,533,968 | 130,932,736–135,770,112 | 122,929,152–123,834,368 |
| timorous | A | 3/3 | 42/20 | 37,820 | 26.4 | 5,116 / 1,957 | 34,972 | 28.6 | 12,907,000–12,907,072 | 12,906,992–12,907,072 | 163,340,288–167,673,856 | 152,137,728–156,954,624 |
| timorous | B | 3/3 | 40/18 | 38,461 | 26.0 | 5,120 / 1,978 | 34,776 | 28.8 | 4,673,960–4,673,992 | 4,673,960–4,673,992 | 151,609,344–162,291,712 | 139,853,824–144,695,296 |

- gfaydark: B vs A median moving frame time -0.01 % (A 33,301 us / 30.0 fps, B 33,298 us / 30.0 fps); Player Movement A 393 us, B 438 us (+0.14 % of A's frame)
- growthplane: B vs A median moving frame time +3.76 % (A 37,120 us / 26.9 fps, B 38,518 us / 26.0 fps); Player Movement A 400 us, B 524 us (+0.33 % of A's frame) -- B SLOWER THAN A BY >3 %
- iceclad: B vs A median moving frame time -20.91 % (A 33,318 us / 30.0 fps, B 26,352 us / 38.0 fps); Player Movement A 3,023 us, B 3,067 us (+0.13 % of A's frame)
- timorous: B vs A median moving frame time +1.69 % (A 37,820 us / 26.4 fps, B 38,461 us / 26.0 fps); Player Movement A 5,116 us, B 5,120 us (+0.01 % of A's frame)

spatial owner rows at steady_60s (per leg):

| zone | leg | owner rows |
|---|---|---|
| gfaydark | r1_A | misc 8,273,760 |
| gfaydark | r2_A | misc 8,273,728 |
| gfaydark | r3_A | misc 8,273,880 |
| gfaydark | r1_B | sp.object_tris 5,230,264, sp.object_regions 163,864, sp.region_bounds 102,784, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r2_B | sp.object_tris 5,230,160, sp.object_regions 163,864, sp.region_bounds 102,800, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r3_B | sp.object_tris 5,230,072, sp.object_regions 163,864, sp.region_bounds 102,816, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| growthplane | r1_A | misc 44,097,376 |
| growthplane | r2_A | misc 44,097,480 |
| growthplane | r3_A | misc 44,097,328 |
| growthplane | r1_B | sp.object_tris 40,188,088, sp.object_regions 327,704, sp.region_bounds 175,896, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| growthplane | r2_B | sp.object_tris 40,188,088, sp.object_regions 327,704, sp.region_bounds 175,864, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| growthplane | r3_B | sp.object_tris 40,188,096, sp.object_regions 327,704, sp.region_bounds 175,904, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| iceclad | r1_A | misc 7,093,888 |
| iceclad | r2_A | misc 7,093,944 |
| iceclad | r3_A | misc 7,093,896 |
| iceclad | r1_B | sp.region_bounds 1,144,824, sp.object_tris 335,496, sp.region_tris 48,736, sp.object_regions 2,584, misc 2,040, sp.light_regions 152 |
| iceclad | r2_B | sp.region_bounds 1,144,816, sp.object_tris 335,480, sp.region_tris 48,872, sp.object_regions 2,584, misc 2,040, sp.light_regions 152 |
| iceclad | r3_B | sp.region_bounds 1,144,824, sp.object_tris 335,496, sp.region_tris 48,872, sp.object_regions 2,584, misc 2,040, sp.light_regions 152 |
| timorous | r1_A | misc 12,907,000 |
| timorous | r2_A | misc 12,906,992 |
| timorous | r3_A | misc 12,907,072 |
| timorous | r1_B | sp.object_tris 3,744,800, sp.region_bounds 844,752, sp.object_regions 40,984, sp.region_tris 38,424, misc 5,032 |
| timorous | r2_B | sp.object_tris 3,744,800, sp.region_bounds 844,744, sp.object_regions 40,984, sp.region_tris 38,424, misc 5,032 |
| timorous | r3_B | sp.object_tris 3,744,776, sp.region_bounds 844,744, sp.object_regions 40,984, sp.region_tris 38,424, misc 5,032 |
