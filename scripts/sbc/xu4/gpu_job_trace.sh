#!/usr/bin/env bash
# trace1.sh <label> <glmark args...>  -- env is inherited; prints fps + per-slot GPU job ms
T=/sys/kernel/debug/tracing
label=$1; shift
sudo sh -c "echo > $T/trace; echo 1 > $T/events/dma_fence/dma_fence_init/enable; echo 1 > $T/events/dma_fence/dma_fence_signaled/enable; echo 1 > $T/tracing_on"
fps=$(timeout 90 glmark2-es2-drm "$@" 2>&1 | grep -oE "FPS: [0-9]+" | head -1)
sudo sh -c "echo 0 > $T/tracing_on; cat $T/trace" | grep -E "panfrost-js" | awk -v fps="$fps" -v label="$label" '
  /dma_fence_init/{split($0,a,"timeline="); split(a[2],b," "); tl=b[1]; split($0,s,"seqno="); t0[tl s[2]]=$4}
  /dma_fence_signaled/{split($0,a,"timeline="); split(a[2],b," "); tl=b[1]; split($0,s,"seqno="); k=tl s[2]; if (k in t0){ sub(/:/,"",t0[k]); t=$4; sub(/:/,"",t); d=(t-t0[k])*1000; sum[tl]+=d; n[tl]++}}
  END{printf "%-28s %s", label, fps; for (x in sum) printf "  %s=%.1fms(n=%d)", x, sum[x]/n[x], n[x]; printf "\n"}'
