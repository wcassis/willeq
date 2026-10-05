# Pi microbenchmark: production policy is part of the test

| Run | Render us | Build us | Submit us | Buffer bytes | Production <=2000 us |
|---|---:|---:|---:|---:|---|
| baseline tool default (not production) | 1706.0 | 891.0 | 800.0 | 381424 | Not applicable |
| baseline explicit nobocache | 5771.0 | 923.0 | 4845.0 | 381424 | FAIL |
| optimized production default | 5604.0 | 751.0 | 4845.0 | 358456 | FAIL |
| optimized explicit repeat | 5621.0 | 758.0 | 4834.0 | 358456 | FAIL |

Component medians need not sum to the total median. Both paths issue two draws for the measured run.
The old standalone tool omitted the renderer's B986 policy. Its default number is not a production acceptance pass.
The optimized tool configures the same policy before device creation. Submission alone exceeds the unchanged bar.

Sources:
- /home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-279/pi-micro/drawcost/drawcost.csv (source fd8b002b2a3ecb4f1b7d1a896cad20b9812f4d5d)
- /home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-279/pi-micro/drawcost-nobocache/drawcost.csv (source fd8b002b2a3ecb4f1b7d1a896cad20b9812f4d5d)
- /home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-279/index-micro/capture/drawcost/drawcost.csv (source c81e226090f2960bcd21eac8abee41b2f38ed72b)
- /home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-279/index-micro/capture/drawcost-nobocache/drawcost.csv (source c81e226090f2960bcd21eac8abee41b2f38ed72b)
