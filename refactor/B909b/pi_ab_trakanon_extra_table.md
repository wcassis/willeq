Source: `/mnt/projects/fable/review/B909b/pi-ab3-trakanon`. Frame time = median of the client's `/frametiming` 60-frame blocks ("Total Frame", us); moving = wholly inside the key sequence, idle = after it. fps = median of the blocks' fps estimates.

| zone | side | runs ok | blocks mov/idle | moving frame us | moving fps | Player Movement us (mov / idle) | idle frame us | idle fps | spatial @ready | spatial @steady | peak RssAnon | RssAnon @steady |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| trakanon | A | 4/4 | 82/47 | 25,876 | 38.6 | 312 / 137 | 24,911 | 40.1 | 25,790,496–25,790,544 | 25,790,464–25,790,512 | 149,401,600–152,313,856 | 135,073,792–144,932,864 |
| trakanon | B | 4/4 | 84/45 | 26,189 | 38.1 | 259 / 142 | 24,949 | 40.1 | 712,128–712,168 | 712,096–712,136 | 132,673,536–132,702,208 | 127,913,984–127,942,656 |

- trakanon: B vs A median moving frame time +1.21 % (A 25,876 us / 38.6 fps, B 26,189 us / 38.1 fps); Player Movement A 312 us, B 259 us (-0.21 % of A's frame)

spatial owner rows at steady_60s (per leg):

| zone | leg | owner rows |
|---|---|---|
| trakanon | r1_A | sp.object_tris 25,393,704, sp.region_bounds 212,288, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r2_A | sp.object_tris 25,393,680, sp.region_bounds 212,344, sp.object_regions 163,864, sp.region_tris 13,712, misc 5,864, sp.light_regions 1,048 |
| trakanon | r3_A | sp.object_tris 25,393,672, sp.region_bounds 212,296, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r4_A | sp.object_tris 25,393,680, sp.region_bounds 212,296, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r1_B | sp.object_instances 315,320, sp.region_bounds 212,296, sp.object_regions 163,864, sp.region_tris 13,712, misc 5,864, sp.light_regions 1,048 |
| trakanon | r2_B | sp.object_instances 315,320, sp.region_bounds 212,320, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r3_B | sp.object_instances 315,320, sp.region_bounds 212,288, sp.object_regions 163,864, sp.region_tris 13,712, misc 5,864, sp.light_regions 1,048 |
| trakanon | r4_B | sp.object_instances 315,320, sp.region_bounds 212,312, sp.object_regions 163,864, sp.region_tris 13,712, misc 5,864, sp.light_regions 1,048 |
