#!/bin/bash
cd /home/user/projects/claude/willeq-modetail
DISPLAY=:98 timeout 30 ./build/bin/willeq -c /home/user/projects/claude/summonah.json --debug 5 > /home/user/projects/claude/willeq-modetail/test_output.log 2>&1
