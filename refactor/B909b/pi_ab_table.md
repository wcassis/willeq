Source: `/mnt/projects/fable/review/B909b/pi-ab2`. Frame time = median of the client's `/frametiming` 60-frame blocks ("Total Frame", us); moving = wholly inside the key sequence, idle = after it. fps = median of the blocks' fps estimates.

| zone | side | runs ok | blocks mov/idle | moving frame us | moving fps | Player Movement us (mov / idle) | idle frame us | idle fps | spatial @ready | spatial @steady | peak RssAnon | RssAnon @steady |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| gfaydark | A | 3/3 | 48/24 | 33,316 | 30.0 | 426 / 183 | 33,314 | 30.0 | 5,519,096–5,519,496 | 5,519,064–5,519,464 | 99,905,536–101,629,952 | 93,962,240–95,444,992 |
| gfaydark | B | 3/3 | 46/22 | 33,312 | 30.0 | 411 / 185 | 33,312 | 30.0 | 582,240 | 582,208 | 95,780,864–99,864,576 | 94,978,048–94,998,528 |
| growthplane | A | 3/3 | 38/20 | 38,848 | 25.8 | 562 / 158 | 36,564 | 27.4 | 40,709,184–40,709,264 | 40,709,152–40,709,232 | 161,136,640–170,328,064 | 152,719,360–161,869,824 |
| growthplane | B | 3/3 | 39/20 | 38,266 | 26.1 | 308 / 153 | 39,916 | 25.1 | 966,864–966,896 | 966,832–966,864 | 138,809,344–141,500,416 | 134,746,112–137,809,920 |
| trakanon | A | 3/3 | 57/29 | 26,358 | 37.9 | 317 / 142 | 24,958 | 40.1 | 25,790,488–25,790,496 | 25,790,456–25,790,464 | 142,155,776–146,747,392 | 128,118,784–138,510,336 |
| trakanon | B | 3/3 | 59/32 | 27,216 | 36.7 | 264 / 144 | 24,930 | 40.1 | 712,160–712,176 | 712,128–712,144 | 132,718,592–132,743,168 | 124,968,960–127,967,232 |

- gfaydark: B vs A median moving frame time -0.01 % (A 33,316 us / 30.0 fps, B 33,312 us / 30.0 fps); Player Movement A 426 us, B 411 us (-0.04 % of A's frame)
- growthplane: B vs A median moving frame time -1.50 % (A 38,848 us / 25.8 fps, B 38,266 us / 26.1 fps); Player Movement A 562 us, B 308 us (-0.65 % of A's frame)
- trakanon: B vs A median moving frame time +3.26 % (A 26,358 us / 37.9 fps, B 27,216 us / 36.7 fps); Player Movement A 317 us, B 264 us (-0.20 % of A's frame) -- B SLOWER THAN A BY >3 %

spatial owner rows at steady_60s (per leg):

| zone | leg | owner rows |
|---|---|---|
| gfaydark | r1_A | sp.object_tris 5,230,080, sp.object_regions 163,864, sp.region_bounds 102,800, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r2_A | sp.object_tris 5,230,472, sp.object_regions 163,864, sp.region_bounds 102,808, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r3_A | sp.object_tris 5,230,184, sp.object_regions 163,864, sp.region_bounds 102,744, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r1_B | sp.object_instances 293,224, sp.object_regions 163,864, sp.region_bounds 102,800, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r2_B | sp.object_instances 293,224, sp.object_regions 163,864, sp.region_bounds 102,800, sp.region_tris 16,528, misc 4,744, sp.light_regions 1,048 |
| gfaydark | r3_B | sp.object_instances 293,224, sp.object_regions 163,864, sp.region_bounds 102,792, sp.region_tris 16,536, misc 4,744, sp.light_regions 1,048 |
| growthplane | r1_A | sp.object_tris 40,188,088, sp.object_regions 327,704, sp.region_bounds 175,904, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| growthplane | r2_A | sp.object_tris 40,188,088, sp.object_regions 327,704, sp.region_bounds 175,904, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| growthplane | r3_A | sp.object_tris 40,188,152, sp.object_regions 327,704, sp.region_bounds 175,912, sp.region_tris 11,728, misc 5,456, sp.light_regions 280 |
| growthplane | r1_B | sp.object_instances 445,792, sp.object_regions 327,704, sp.region_bounds 175,904, sp.region_tris 11,728, misc 5,456, sp.light_regions 280 |
| growthplane | r2_B | sp.object_instances 445,792, sp.object_regions 327,704, sp.region_bounds 175,880, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| growthplane | r3_B | sp.object_instances 445,792, sp.object_regions 327,704, sp.region_bounds 175,896, sp.region_tris 11,720, misc 5,456, sp.light_regions 280 |
| trakanon | r1_A | sp.object_tris 25,393,672, sp.region_bounds 212,288, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r2_A | sp.object_tris 25,393,664, sp.region_bounds 212,296, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r3_A | sp.object_tris 25,393,672, sp.region_bounds 212,296, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r1_B | sp.object_instances 315,320, sp.region_bounds 212,320, sp.object_regions 163,864, sp.region_tris 13,728, misc 5,864, sp.light_regions 1,048 |
| trakanon | r2_B | sp.object_instances 315,320, sp.region_bounds 212,312, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
| trakanon | r3_B | sp.object_instances 315,320, sp.region_bounds 212,320, sp.object_regions 163,864, sp.region_tris 13,720, misc 5,864, sp.light_regions 1,048 |
