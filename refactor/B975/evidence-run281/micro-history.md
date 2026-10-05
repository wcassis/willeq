# Run 281 native Pi candidates

Unchanged 2000 us limit; production Lima policy and pinned Mesa 25.2.8.
Component medians are independent, so their sum need not equal the total.

| Candidate | Mode | render us | build us | submit us | Verdict |
|---|---|---:|---:|---:|---|
| reusable GPU storage (`7374b642`) | drawcost | 2221.0 | 765.0 | 1442.0 | fail |
| reusable GPU storage (`7374b642`) | drawcost-nobocache | 2203.0 | 772.0 | 1409.0 | fail |
| packed corners (`27446f74`) | drawcost | 2144.0 | 718.0 | 1405.0 | fail |
| packed corners (`27446f74`) | drawcost-nobocache | 2159.0 | 724.0 | 1418.0 | fail |
| retained prefix (`af428c8a`) | drawcost | 2046.0 | 581.0 | 1446.0 | fail |
| retained prefix (`af428c8a`) | drawcost-nobocache | 2017.0 | 573.0 | 1439.0 | fail |
| cached UVs (`850b679c`) | drawcost | 1917.0 | 479.0 | 1420.0 | pass |
| cached UVs (`850b679c`) | drawcost-nobocache | 1963.0 | 489.0 | 1458.0 | pass |

Exact source paths and content hashes: `micro-history.json`.
These are candidate measurements, not final-head acceptance. Earlier failures remain failures.
