#!/bin/bash
# Script to debug music player crash with gdb
# Run this, connect via RDP, open a vendor window to trigger crash

cd /home/user/projects/claude/willeq-rdp

# Create gdb commands file
cat > /tmp/gdb_commands.txt << 'EOF'
set pagination off
set confirm off

# Break on std::terminate (the actual crash)
break std::terminate
break __cxa_throw

# Commands to run when terminate is hit
commands 1
  echo \n=== CRASH: std::terminate called ===\n
  bt full
  echo \n=== All threads ===\n
  info threads
  thread apply all bt
  quit
end

# For throw, just continue (many exceptions are caught)
commands 2
  continue
end

run -c /home/user/projects/claude/summonah.json --soundfont /home/user/projects/claude/willeq-rdp/data/1mgm.sf2 --rdp -d 1
EOF

echo "Starting willeq with gdb..."
echo "Connect via RDP and open a vendor window to trigger the crash"
echo "Output will be saved to gdb_music_crash.log"
echo ""

DISPLAY=:99 gdb -x /tmp/gdb_commands.txt ./build/bin/willeq 2>&1 | tee gdb_music_crash.log

echo ""
echo "Done. Check gdb_music_crash.log for full output"
