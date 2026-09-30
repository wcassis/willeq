## Per run

| zone | side | rnd | verdict | RssAnon st/rdy/stdy MiB | RssFile st/rdy/stdy MiB | gpu_driver st/rdy/stdy MiB | owned (RssAnon-gpu_driver) rdy/stdy/max MiB | peak RssAnon | VmHWM | driver .so Rss | fps med/min | libLLVM mapped | libgallium |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gfaydark | A | 1 | pass | 28.5/89.6/89.6 | 54.6/71.1/64.5 | 17.8/18.9/19.2 | 71.3/70.4/71.3 | 95.2 | 163.9 | 40.1 | 30/10 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| gfaydark | A | 2 | pass | 28.5/91.7/91.0 | 53.3/72.7/65.4 | 17.8/19.0/19.1 | 72.7/72.0/72.7 | 95.7 | 164.4 | 39.2 | 30/10 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| gfaydark | B | 1 | pass | 21.8/84.2/83.5 | 25.4/45.0/35.6 | 17.8/19.0/19.1 | 65.2/64.5/65.2 | 89.2 | 129.3 | 5.8 | 30/10 | False | libgallium-25.2.8.so |
| gfaydark | B | 2 | pass | 21.7/87.8/87.2 | 24.8/44.1/37.1 | 17.8/18.9/19.0 | 68.9/68.2/68.9 | 88.5 | 132.0 | 5.4 | 30/10 | False | libgallium-25.2.8.so |
| growthplane | A | 1 | pass | 28.5/157.3/157.3 | 54.2/56.7/56.4 | 17.8/18.5/18.5 | 138.9/138.9/139.4 | 164.4 | 221.2 | 38.0 | 20/1 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| growthplane | A | 2 | pass | 28.5/144.1/144.9 | 54.9/54.4/54.7 | 17.8/18.4/18.4 | 125.5/126.5/126.5 | 151.3 | 211.4 | 38.1 | 20/19 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| growthplane | B | 1 | pass | 21.8/149.5/149.5 | 24.4/30.3/30.5 | 17.8/18.3/18.4 | 131.2/131.2/131.7 | 156.6 | 187.0 | 5.6 | 20/19 | False | libgallium-25.2.8.so |
| growthplane | B | 2 | pass | 21.8/150.9/150.9 | 25.1/30.6/30.7 | 17.8/18.3/18.4 | 132.6/132.6/133.1 | 157.9 | 188.7 | 5.7 | 20/19 | False | libgallium-25.2.8.so |
| iceclad | A | 1 | pass | 28.5/104.1/104.2 | 54.1/59.0/58.2 | 17.8/18.6/18.6 | 85.5/85.6/92.1 | 110.8 | 173.3 | 40.2 | 30.0/10 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| iceclad | A | 2 | pass | 28.5/104.2/104.3 | 56.3/63.8/55.8 | 17.8/18.6/18.6 | 85.6/85.7/92.2 | 111.3 | 175.5 | 41.9 | 30/9 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| iceclad | B | 1 | pass | 21.9/97.6/97.7 | 24.5/29.8/28.3 | 17.8/18.6/18.6 | 79.0/79.1/85.7 | 104.4 | 136.5 | 5.5 | 30/9 | False | libgallium-25.2.8.so |
| iceclad | B | 2 | pass | 21.8/97.4/97.5 | 24.7/29.2/28.9 | 17.8/18.6/18.6 | 78.8/78.9/85.4 | 104.0 | 137.1 | 5.7 | 30/10 | False | libgallium-25.2.8.so |
| qeynos2 | A | 1 | pass | 28.5/65.3/65.3 | 53.3/65.4/64.6 | 17.8/19.1/19.2 | 46.2/46.2/46.2 | 75.9 | 140.3 | 39.5 | 60/16 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| qeynos2 | A | 2 | pass | 28.5/70.7/70.7 | 57.4/70.5/67.1 | 17.8/19.2/19.2 | 51.5/51.4/51.5 | 81.2 | 149.7 | 43.4 | 60/43 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| qeynos2 | B | 1 | pass | 21.9/64.1/64.1 | 25.1/37.2/37.8 | 17.8/19.2/19.2 | 44.9/44.9/44.9 | 74.7 | 112.4 | 5.7 | 60/42 | False | libgallium-25.2.8.so |
| qeynos2 | B | 2 | pass | 21.9/63.6/63.6 | 24.2/37.0/37.4 | 17.8/19.1/19.2 | 44.5/44.4/44.5 | 74.1 | 111.4 | 5.5 | 60.0/27 | False | libgallium-25.2.8.so |
| timorous | A | 1 | pass | 28.5/133.3/133.3 | 55.9/67.2/53.4 | 17.8/19.0/19.0 | 114.2/114.2/117.3 | 141.3 | 201.6 | 39.8 | 30/9 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| timorous | A | 2 | pass | 28.4/130.8/126.2 | 58.4/71.3/59.2 | 17.8/19.0/19.1 | 111.8/107.1/114.8 | 138.7 | 202.1 | 38.9 | 30/10 | True | libgallium-25.2.8-0ubuntu0.24.04.1.so |
| timorous | B | 1 | pass | 21.8/122.8/122.8 | 25.6/35.1/22.6 | 17.8/19.0/19.0 | 103.7/103.8/108.2 | 135.0 | 166.3 | 5.5 | 30/10 | False | libgallium-25.2.8.so |
| timorous | B | 2 | pass | 21.8/124.8/124.9 | 25.3/34.0/30.2 | 17.8/19.0/19.0 | 105.9/105.9/108.9 | 132.8 | 170.1 | 5.7 | 30/10 | False | libgallium-25.2.8.so |

## Per zone, medians over passing runs (B - A)

| zone | metric | A stock MiB | B no-LLVM MiB | B-A MiB |
|---|---|---|---|---|
| gfaydark | RssFile steady | 64.9 | 36.4 | -28.6 |
| gfaydark | RssAnon steady | 90.3 | 85.3 | -5.0 |
| gfaydark | gpu_driver steady | 19.1 | 19.0 | -0.1 |
| gfaydark | owned steady | 71.2 | 66.3 | -4.9 |
| gfaydark | owned max | 72.0 | 67.0 | -4.9 |
| gfaydark | peak RssAnon | 95.4 | 88.9 | -6.6 |
| gfaydark | VmHWM | 164.2 | 130.6 | -33.5 |
| gfaydark | driver .so Rss | 39.7 | 5.6 | -34.1 |
| gfaydark | fps median (runs) | [30, 30] | [30, 30] | |
| gfaydark | owned max (worst run) minus 64 MiB | +8.7 | +4.9 | |
| growthplane | RssFile steady | 55.5 | 30.6 | -25.0 |
| growthplane | RssAnon steady | 151.1 | 150.2 | -0.9 |
| growthplane | gpu_driver steady | 18.4 | 18.4 | -0.1 |
| growthplane | owned steady | 132.7 | 131.9 | -0.8 |
| growthplane | owned max | 132.9 | 132.4 | -0.6 |
| growthplane | peak RssAnon | 157.8 | 157.3 | -0.6 |
| growthplane | VmHWM | 216.3 | 187.8 | -28.5 |
| growthplane | driver .so Rss | 38.0 | 5.6 | -32.4 |
| growthplane | fps median (runs) | [20, 20] | [20, 20] | |
| growthplane | owned max (worst run) minus 64 MiB | +75.4 | +69.1 | |
| iceclad | RssFile steady | 57.0 | 28.6 | -28.4 |
| iceclad | RssAnon steady | 104.3 | 97.6 | -6.7 |
| iceclad | gpu_driver steady | 18.6 | 18.6 | -0.0 |
| iceclad | owned steady | 85.6 | 79.0 | -6.6 |
| iceclad | owned max | 92.2 | 85.5 | -6.6 |
| iceclad | peak RssAnon | 111.0 | 104.2 | -6.8 |
| iceclad | VmHWM | 174.4 | 136.8 | -37.6 |
| iceclad | driver .so Rss | 41.1 | 5.6 | -35.5 |
| iceclad | fps median (runs) | [30.0, 30] | [30, 30] | |
| iceclad | owned max (worst run) minus 64 MiB | +28.2 | +21.7 | |
| qeynos2 | RssFile steady | 65.8 | 37.6 | -28.2 |
| qeynos2 | RssAnon steady | 68.0 | 63.8 | -4.2 |
| qeynos2 | gpu_driver steady | 19.2 | 19.2 | +0.0 |
| qeynos2 | owned steady | 48.8 | 44.6 | -4.2 |
| qeynos2 | owned max | 48.8 | 44.7 | -4.1 |
| qeynos2 | peak RssAnon | 78.6 | 74.4 | -4.2 |
| qeynos2 | VmHWM | 145.0 | 111.9 | -33.1 |
| qeynos2 | driver .so Rss | 41.5 | 5.6 | -35.9 |
| qeynos2 | fps median (runs) | [60, 60] | [60, 60.0] | |
| qeynos2 | owned max (worst run) minus 64 MiB | -12.5 | -19.1 | |
| timorous | RssFile steady | 56.3 | 26.4 | -29.9 |
| timorous | RssAnon steady | 129.7 | 123.8 | -5.9 |
| timorous | gpu_driver steady | 19.0 | 19.0 | -0.0 |
| timorous | owned steady | 110.7 | 104.8 | -5.8 |
| timorous | owned max | 116.1 | 108.6 | -7.5 |
| timorous | peak RssAnon | 140.0 | 133.9 | -6.0 |
| timorous | VmHWM | 201.9 | 168.2 | -33.7 |
| timorous | driver .so Rss | 39.3 | 5.6 | -33.8 |
| timorous | fps median (runs) | [30, 30] | [30, 30] | |
| timorous | owned max (worst run) minus 64 MiB | +53.3 | +44.9 | |

## Driver shared objects, resident at steady_60s (median over passing runs)

| zone | library | A Rss MiB | A anon MiB | B Rss MiB | B anon MiB |
|---|---|---|---|---|---|
| gfaydark | libLLVM.so.20.1 | 31.5 | 5.1 | absent | - |
| gfaydark | libgallium-25.2.8-0ubuntu0.24.04.1.so | 7.2 | 1.0 | absent | - |
| gfaydark | libGLdispatch.so.0.0.0 | 0.4 | 0.1 | 0.4 | 0.1 |
| gfaydark | libicuuc.so.74.2 | 0.4 | 0.0 | absent | - |
| gfaydark | libEGL_mesa.so.0.0.0 | 0.2 | 0.0 | 0.2 | 0.0 |
| gfaydark | libxml2.so.2.9.14 | 0.1 | 0.0 | absent | - |
| gfaydark | libGLESv2.so.2.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| gfaydark | libtinfo.so.6.4 | 0.1 | 0.0 | absent | - |
| gfaydark | libicudata.so.74.2 | 0.0 | 0.0 | absent | - |
| gfaydark | libedit.so.2.0.72 | 0.1 | 0.0 | absent | - |
| gfaydark | libelf-0.190.so | 0.1 | 0.0 | absent | - |
| gfaydark | libdrm.so.2.125.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| gfaydark | libEGL.so.1.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| gfaydark | libxcb-randr.so.0.1.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libsensors.so.5.0.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libwayland-client.so.0.22.0 | 0.0 | 0.0 | absent | - |
| gfaydark | dri_gbm.so | 0.0 | 0.0 | 0.0 | 0.0 |
| gfaydark | libdrm_amdgpu.so.1.125.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libffi.so.8.1.4 | 0.0 | 0.0 | absent | - |
| gfaydark | libxcb-xfixes.so.0.0.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libxcb-sync.so.1.0.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libxcb-dri3.so.0.1.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libxcb-present.so.0.0.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libgbm.so.1.0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| gfaydark | libxcb-shm.so.0.0.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libxshmfence.so.1.0.0 | 0.0 | 0.0 | absent | - |
| gfaydark | libgallium-25.2.8.so | absent | - | 4.8 | 0.3 |
| growthplane | libLLVM.so.20.1 | 30.3 | 4.1 | absent | - |
| growthplane | libgallium-25.2.8-0ubuntu0.24.04.1.so | 6.9 | 0.9 | absent | - |
| growthplane | libicuuc.so.74.2 | 0.4 | 0.0 | absent | - |
| growthplane | libGLdispatch.so.0.0.0 | 0.3 | 0.1 | 0.4 | 0.1 |
| growthplane | libEGL_mesa.so.0.0.0 | 0.2 | 0.0 | 0.2 | 0.0 |
| growthplane | libxml2.so.2.9.14 | 0.1 | 0.0 | absent | - |
| growthplane | libGLESv2.so.2.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| growthplane | libtinfo.so.6.4 | 0.1 | 0.0 | absent | - |
| growthplane | libicudata.so.74.2 | 0.0 | 0.0 | absent | - |
| growthplane | libedit.so.2.0.72 | 0.1 | 0.0 | absent | - |
| growthplane | libelf-0.190.so | 0.1 | 0.0 | absent | - |
| growthplane | libdrm.so.2.125.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| growthplane | libEGL.so.1.1.0 | 0.1 | 0.0 | 0.0 | 0.0 |
| growthplane | libxcb-randr.so.0.1.0 | 0.0 | 0.0 | absent | - |
| growthplane | libsensors.so.5.0.0 | 0.0 | 0.0 | absent | - |
| growthplane | dri_gbm.so | 0.0 | 0.0 | 0.0 | 0.0 |
| growthplane | libwayland-client.so.0.22.0 | 0.0 | 0.0 | absent | - |
| growthplane | libdrm_amdgpu.so.1.125.0 | 0.0 | 0.0 | absent | - |
| growthplane | libxcb-xfixes.so.0.0.0 | 0.0 | 0.0 | absent | - |
| growthplane | libffi.so.8.1.4 | 0.0 | 0.0 | absent | - |
| growthplane | libxcb-sync.so.1.0.0 | 0.0 | 0.0 | absent | - |
| growthplane | libxcb-dri3.so.0.1.0 | 0.0 | 0.0 | absent | - |
| growthplane | libxcb-present.so.0.0.0 | 0.0 | 0.0 | absent | - |
| growthplane | libgbm.so.1.0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| growthplane | libxcb-shm.so.0.0.0 | 0.0 | 0.0 | absent | - |
| growthplane | libxshmfence.so.1.0.0 | 0.0 | 0.0 | absent | - |
| growthplane | libgallium-25.2.8.so | absent | - | 4.8 | 0.3 |
| iceclad | libLLVM.so.20.1 | 32.9 | 5.1 | absent | - |
| iceclad | libgallium-25.2.8-0ubuntu0.24.04.1.so | 7.4 | 1.0 | absent | - |
| iceclad | libicuuc.so.74.2 | 0.4 | 0.0 | absent | - |
| iceclad | libGLdispatch.so.0.0.0 | 0.3 | 0.1 | 0.4 | 0.1 |
| iceclad | libEGL_mesa.so.0.0.0 | 0.2 | 0.0 | 0.2 | 0.0 |
| iceclad | libxml2.so.2.9.14 | 0.2 | 0.0 | absent | - |
| iceclad | libGLESv2.so.2.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| iceclad | libtinfo.so.6.4 | 0.1 | 0.0 | absent | - |
| iceclad | libicudata.so.74.2 | 0.0 | 0.0 | absent | - |
| iceclad | libedit.so.2.0.72 | 0.1 | 0.0 | absent | - |
| iceclad | libelf-0.190.so | 0.1 | 0.0 | absent | - |
| iceclad | libdrm.so.2.125.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| iceclad | libEGL.so.1.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| iceclad | libxcb-randr.so.0.1.0 | 0.0 | 0.0 | absent | - |
| iceclad | libsensors.so.5.0.0 | 0.0 | 0.0 | absent | - |
| iceclad | libwayland-client.so.0.22.0 | 0.0 | 0.0 | absent | - |
| iceclad | dri_gbm.so | 0.0 | 0.0 | 0.0 | 0.0 |
| iceclad | libdrm_amdgpu.so.1.125.0 | 0.0 | 0.0 | absent | - |
| iceclad | libffi.so.8.1.4 | 0.0 | 0.0 | absent | - |
| iceclad | libxcb-xfixes.so.0.0.0 | 0.0 | 0.0 | absent | - |
| iceclad | libxcb-sync.so.1.0.0 | 0.0 | 0.0 | absent | - |
| iceclad | libxcb-dri3.so.0.1.0 | 0.0 | 0.0 | absent | - |
| iceclad | libxcb-present.so.0.0.0 | 0.0 | 0.0 | absent | - |
| iceclad | libgbm.so.1.0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| iceclad | libxcb-shm.so.0.0.0 | 0.0 | 0.0 | absent | - |
| iceclad | libxshmfence.so.1.0.0 | 0.0 | 0.0 | absent | - |
| iceclad | libgallium-25.2.8.so | absent | - | 4.8 | 0.3 |
| qeynos2 | libLLVM.so.20.1 | 33.2 | 5.1 | absent | - |
| qeynos2 | libgallium-25.2.8-0ubuntu0.24.04.1.so | 7.5 | 1.0 | absent | - |
| qeynos2 | libicuuc.so.74.2 | 0.4 | 0.0 | absent | - |
| qeynos2 | libGLdispatch.so.0.0.0 | 0.4 | 0.1 | 0.4 | 0.1 |
| qeynos2 | libEGL_mesa.so.0.0.0 | 0.2 | 0.0 | 0.2 | 0.0 |
| qeynos2 | libxml2.so.2.9.14 | 0.2 | 0.0 | absent | - |
| qeynos2 | libGLESv2.so.2.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| qeynos2 | libtinfo.so.6.4 | 0.1 | 0.0 | absent | - |
| qeynos2 | libicudata.so.74.2 | 0.0 | 0.0 | absent | - |
| qeynos2 | libedit.so.2.0.72 | 0.1 | 0.0 | absent | - |
| qeynos2 | libelf-0.190.so | 0.1 | 0.0 | absent | - |
| qeynos2 | libdrm.so.2.125.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| qeynos2 | libEGL.so.1.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| qeynos2 | libxcb-randr.so.0.1.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libsensors.so.5.0.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libwayland-client.so.0.22.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | dri_gbm.so | 0.0 | 0.0 | 0.0 | 0.0 |
| qeynos2 | libdrm_amdgpu.so.1.125.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libffi.so.8.1.4 | 0.0 | 0.0 | absent | - |
| qeynos2 | libxcb-xfixes.so.0.0.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libxcb-sync.so.1.0.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libxcb-dri3.so.0.1.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libxcb-present.so.0.0.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libgbm.so.1.0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| qeynos2 | libxcb-shm.so.0.0.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libxshmfence.so.1.0.0 | 0.0 | 0.0 | absent | - |
| qeynos2 | libgallium-25.2.8.so | absent | - | 4.8 | 0.3 |
| timorous | libLLVM.so.20.1 | 31.7 | 3.3 | absent | - |
| timorous | libgallium-25.2.8-0ubuntu0.24.04.1.so | 6.9 | 0.6 | absent | - |
| timorous | libGLdispatch.so.0.0.0 | 0.3 | 0.0 | 0.3 | 0.0 |
| timorous | libicuuc.so.74.2 | 0.3 | 0.0 | absent | - |
| timorous | libEGL_mesa.so.0.0.0 | 0.2 | 0.0 | 0.2 | 0.0 |
| timorous | libxml2.so.2.9.14 | 0.1 | 0.0 | absent | - |
| timorous | libGLESv2.so.2.1.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| timorous | libtinfo.so.6.4 | 0.1 | 0.0 | absent | - |
| timorous | libicudata.so.74.2 | 0.0 | 0.0 | absent | - |
| timorous | libedit.so.2.0.72 | 0.1 | 0.0 | absent | - |
| timorous | libelf-0.190.so | 0.0 | 0.0 | absent | - |
| timorous | libdrm.so.2.125.0 | 0.1 | 0.0 | 0.1 | 0.0 |
| timorous | libxcb-randr.so.0.1.0 | 0.0 | 0.0 | absent | - |
| timorous | libsensors.so.5.0.0 | 0.0 | 0.0 | absent | - |
| timorous | libwayland-client.so.0.22.0 | 0.0 | 0.0 | absent | - |
| timorous | libEGL.so.1.1.0 | 0.0 | 0.0 | 0.1 | 0.0 |
| timorous | libdrm_amdgpu.so.1.125.0 | 0.0 | 0.0 | absent | - |
| timorous | libxcb-xfixes.so.0.0.0 | 0.0 | 0.0 | absent | - |
| timorous | dri_gbm.so | 0.0 | 0.0 | 0.0 | 0.0 |
| timorous | libffi.so.8.1.4 | 0.0 | 0.0 | absent | - |
| timorous | libxcb-sync.so.1.0.0 | 0.0 | 0.0 | absent | - |
| timorous | libxcb-dri3.so.0.1.0 | 0.0 | 0.0 | absent | - |
| timorous | libxcb-present.so.0.0.0 | 0.0 | 0.0 | absent | - |
| timorous | libgbm.so.1.0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| timorous | libxcb-shm.so.0.0.0 | 0.0 | 0.0 | absent | - |
| timorous | libxshmfence.so.1.0.0 | 0.0 | 0.0 | absent | - |
| timorous | libgallium-25.2.8.so | absent | - | 4.9 | 0.3 |

## Frames: pairwise diff of the dumped frames, same zone, same frame index

| zone | frame | pair | kind | mean abs | max abs | % px > 8 | SSIM |
|---|---|---|---|---|---|---|---|
| gfaydark | frame_000000.png | A1 vs B1 | cross | 10.786 | 255 | 81.813 | 0.95439 |
| gfaydark | frame_000001.png | A1 vs B1 | cross | 10.786 | 255 | 81.857 | 0.95471 |
| gfaydark | frame_000002.png | A1 vs B1 | cross | 10.786 | 255 | 81.695 | 0.95468 |
| gfaydark | frame_000000.png | A1 vs A2 | within-A | 19.491 | 214 | 92.701 | 0.88942 |
| gfaydark | frame_000001.png | A1 vs A2 | within-A | 19.482 | 214 | 92.626 | 0.89232 |
| gfaydark | frame_000002.png | A1 vs A2 | within-A | 19.478 | 214 | 92.808 | 0.89125 |
| gfaydark | frame_000000.png | A1 vs B2 | cross | 9.787 | 239 | 81.337 | 0.98288 |
| gfaydark | frame_000001.png | A1 vs B2 | cross | 9.807 | 239 | 81.403 | 0.98307 |
| gfaydark | frame_000002.png | A1 vs B2 | cross | 9.792 | 239 | 81.279 | 0.98306 |
| gfaydark | frame_000000.png | B1 vs A2 | cross | 29.932 | 255 | 94.249 | 0.79531 |
| gfaydark | frame_000001.png | B1 vs A2 | cross | 29.939 | 255 | 94.234 | 0.79931 |
| gfaydark | frame_000002.png | B1 vs A2 | cross | 29.86 | 255 | 94.396 | 0.79808 |
| gfaydark | frame_000000.png | B1 vs B2 | within-B | 1.056 | 255 | 0.812 | 0.97087 |
| gfaydark | frame_000001.png | B1 vs B2 | within-B | 1.059 | 255 | 0.813 | 0.97093 |
| gfaydark | frame_000002.png | B1 vs B2 | within-B | 1.084 | 255 | 0.813 | 0.9708 |
| gfaydark | frame_000000.png | A2 vs B2 | cross | 28.961 | 246 | 93.82 | 0.82249 |
| gfaydark | frame_000001.png | A2 vs B2 | cross | 28.988 | 246 | 93.811 | 0.8263 |
| gfaydark | frame_000002.png | A2 vs B2 | cross | 28.922 | 246 | 93.962 | 0.82494 |
| growthplane | frame_000000.png | A1 vs B1 | cross | 69.34 | 206 | 96.459 | 0.62379 |
| growthplane | frame_000001.png | A1 vs B1 | cross | 69.343 | 206 | 96.458 | 0.62356 |
| growthplane | frame_000002.png | A1 vs B1 | cross | 69.34 | 206 | 96.465 | 0.62411 |
| growthplane | frame_000000.png | A1 vs A2 | within-A | 42.995 | 122 | 96.408 | 0.75555 |
| growthplane | frame_000001.png | A1 vs A2 | within-A | 42.996 | 122 | 96.408 | 0.75544 |
| growthplane | frame_000002.png | A1 vs A2 | within-A | 42.99 | 122 | 96.409 | 0.75597 |
| growthplane | frame_000000.png | A1 vs B2 | cross | 69.595 | 172 | 96.611 | 0.62832 |
| growthplane | frame_000001.png | A1 vs B2 | cross | 69.595 | 172 | 96.609 | 0.62778 |
| growthplane | frame_000002.png | A1 vs B2 | cross | 69.594 | 172 | 96.61 | 0.62796 |
| growthplane | frame_000000.png | B1 vs A2 | cross | 26.382 | 170 | 90.347 | 0.94559 |
| growthplane | frame_000001.png | B1 vs A2 | cross | 26.376 | 170 | 90.346 | 0.94625 |
| growthplane | frame_000002.png | B1 vs A2 | cross | 26.383 | 170 | 90.344 | 0.94571 |
| growthplane | frame_000000.png | B1 vs B2 | within-B | 2.363 | 190 | 21.304 | 0.99609 |
| growthplane | frame_000001.png | B1 vs B2 | within-B | 2.365 | 190 | 21.31 | 0.99598 |
| growthplane | frame_000002.png | B1 vs B2 | within-B | 2.367 | 190 | 21.327 | 0.99586 |
| growthplane | frame_000000.png | A2 vs B2 | cross | 26.632 | 120 | 90.34 | 0.95089 |
| growthplane | frame_000001.png | A2 vs B2 | cross | 26.632 | 117 | 90.34 | 0.95087 |
| growthplane | frame_000002.png | A2 vs B2 | cross | 26.635 | 122 | 90.341 | 0.95068 |
| iceclad | frame_000000.png | A1 vs B1 | cross | 87.649 | 194 | 97.675 | 0.24676 |
| iceclad | frame_000001.png | A1 vs B1 | cross | 87.63 | 194 | 97.571 | 0.24858 |
| iceclad | frame_000002.png | A1 vs B1 | cross | 87.67 | 194 | 97.531 | 0.24678 |
| iceclad | frame_000000.png | A1 vs A2 | within-A | 84.231 | 194 | 97.546 | 0.26525 |
| iceclad | frame_000001.png | A1 vs A2 | within-A | 84.247 | 194 | 97.549 | 0.26558 |
| iceclad | frame_000002.png | A1 vs A2 | within-A | 84.232 | 194 | 97.586 | 0.26545 |
| iceclad | frame_000000.png | A1 vs B2 | cross | 92.431 | 194 | 97.99 | 0.24711 |
| iceclad | frame_000001.png | A1 vs B2 | cross | 92.42 | 194 | 97.938 | 0.24759 |
| iceclad | frame_000002.png | A1 vs B2 | cross | 92.425 | 194 | 97.786 | 0.24704 |
| iceclad | frame_000000.png | B1 vs A2 | cross | 4.515 | 134 | 38.402 | 0.95994 |
| iceclad | frame_000001.png | B1 vs A2 | cross | 4.333 | 48 | 36.877 | 0.96705 |
| iceclad | frame_000002.png | B1 vs A2 | cross | 4.47 | 117 | 38.033 | 0.9615 |
| iceclad | frame_000000.png | B1 vs B2 | within-B | 10.049 | 45 | 64.327 | 0.97012 |
| iceclad | frame_000001.png | B1 vs B2 | within-B | 10.221 | 136 | 65.917 | 0.96045 |
| iceclad | frame_000002.png | B1 vs B2 | within-B | 10.041 | 49 | 64.045 | 0.96743 |
| iceclad | frame_000000.png | A2 vs B2 | cross | 13.6 | 143 | 69.755 | 0.95827 |
| iceclad | frame_000001.png | A2 vs B2 | cross | 13.607 | 143 | 69.262 | 0.95861 |
| iceclad | frame_000002.png | A2 vs B2 | cross | 13.581 | 124 | 69.352 | 0.95828 |
| qeynos2 | frame_000000.png | A1 vs B1 | cross | 35.701 | 236 | 93.56 | 0.8597 |
| qeynos2 | frame_000001.png | A1 vs B1 | cross | 35.69 | 190 | 93.556 | 0.8599 |
| qeynos2 | frame_000002.png | A1 vs B1 | cross | 35.687 | 190 | 93.556 | 0.85983 |
| qeynos2 | frame_000000.png | A1 vs A2 | within-A | 9.379 | 202 | 44.714 | 0.95569 |
| qeynos2 | frame_000001.png | A1 vs A2 | within-A | 9.38 | 202 | 44.714 | 0.95561 |
| qeynos2 | frame_000002.png | A1 vs A2 | within-A | 9.388 | 240 | 44.715 | 0.95555 |
| qeynos2 | frame_000000.png | A1 vs B2 | cross | 34.065 | 229 | 93.551 | 0.87146 |
| qeynos2 | frame_000001.png | A1 vs B2 | cross | 34.067 | 191 | 93.551 | 0.87139 |
| qeynos2 | frame_000002.png | A1 vs B2 | cross | 34.06 | 191 | 93.551 | 0.87145 |
| qeynos2 | frame_000000.png | B1 vs A2 | cross | 45.001 | 244 | 93.635 | 0.75799 |
| qeynos2 | frame_000001.png | B1 vs A2 | cross | 44.985 | 150 | 93.631 | 0.75826 |
| qeynos2 | frame_000002.png | B1 vs A2 | cross | 44.993 | 240 | 93.632 | 0.7581 |
| qeynos2 | frame_000000.png | B1 vs B2 | within-B | 2.984 | 227 | 13.892 | 0.99828 |
| qeynos2 | frame_000001.png | B1 vs B2 | within-B | 2.966 | 13 | 13.89 | 0.99867 |
| qeynos2 | frame_000002.png | B1 vs B2 | within-B | 2.971 | 103 | 13.894 | 0.99851 |
| qeynos2 | frame_000000.png | A2 vs B2 | cross | 43.364 | 241 | 93.664 | 0.7698 |
| qeynos2 | frame_000001.png | A2 vs B2 | cross | 43.362 | 158 | 93.664 | 0.76983 |
| qeynos2 | frame_000002.png | A2 vs B2 | cross | 43.365 | 240 | 93.665 | 0.76977 |
| timorous | frame_000000.png | A1 vs B1 | cross | 81.25 | 255 | 97.024 | 0.30639 |
| timorous | frame_000001.png | A1 vs B1 | cross | 81.247 | 166 | 97.023 | 0.30643 |
| timorous | frame_000002.png | A1 vs B1 | cross | 52.762 | 255 | 96.897 | 0.31859 |
| timorous | frame_000000.png | A1 vs A2 | within-A | 90.003 | 184 | 97.26 | 0.3025 |
| timorous | frame_000001.png | A1 vs A2 | within-A | 90.027 | 184 | 97.25 | 0.30034 |
| timorous | frame_000002.png | A1 vs A2 | within-A | 90.877 | 255 | 97.25 | 0.29164 |
| timorous | frame_000000.png | A1 vs B2 | cross | 89.712 | 255 | 97.275 | 0.30342 |
| timorous | frame_000001.png | A1 vs B2 | cross | 89.733 | 194 | 97.263 | 0.30127 |
| timorous | frame_000002.png | A1 vs B2 | cross | 90.602 | 255 | 97.269 | 0.29462 |
| timorous | frame_000000.png | B1 vs A2 | cross | 9.934 | 225 | 53.561 | 0.99433 |
| timorous | frame_000001.png | B1 vs A2 | cross | 10.087 | 56 | 54.632 | 0.98319 |
| timorous | frame_000002.png | B1 vs A2 | cross | 38.872 | 151 | 54.853 | 0.72861 |
| timorous | frame_000000.png | B1 vs B2 | within-B | 12.082 | 66 | 54.711 | 0.99609 |
| timorous | frame_000001.png | B1 vs B2 | within-B | 12.224 | 66 | 55.681 | 0.9849 |
| timorous | frame_000002.png | B1 vs B2 | within-B | 38.715 | 161 | 54.794 | 0.748 |
| timorous | frame_000000.png | A2 vs B2 | cross | 2.746 | 225 | 15.089 | 0.99946 |
| timorous | frame_000001.png | A2 vs B2 | cross | 2.744 | 11 | 15.087 | 0.99951 |
| timorous | frame_000002.png | A2 vs B2 | cross | 3.141 | 41 | 17.786 | 0.98563 |
