#!/bin/bash
MODE=${1:-run}
WILLEQ=./build-arm-noble/bin/willeq
CONFIG=/mnt/projects/summonah.json
ARGS="--no-audio --atlas-path /mnt/projects/EverQuestP1999/cache/ -r 1280 720 -d 5"

case $MODE in
    run)
        # Normal run with perf-friendly settings
        $WILLEQ --config $CONFIG $ARGS > /tmp/willeq.log 2>&1
        ;;
    gdb)
        # Launch under gdbserver, wait for remote connection
        echo "Waiting for GDB connection on port 9999..."
        gdbserver :9999 $WILLEQ --config $CONFIG $ARGS
        ;;
    perf)
        # Run with perf recording
        $WILLEQ --config $CONFIG $ARGS > /tmp/willeq.log 2>&1 &
        PID=$!
        sleep ${2:-80}  # let it start up
        echo "Recording perf data for ${3:-30} seconds..."
        # perf record -g --call-graph dwarf -p $PID -- sleep ${2:-10}
        perf record -g --call-graph fp -p $PID -- sleep ${3:-30}
        kill $PID
        perf report --stdio --no-children > perf_report.txt
        perf report --stdio -g --no-children > perf_callgraph.txt
        echo "Reports written to perf_report.txt and perf_callgraph.txt"
        ;;
    strace)
        # Trace system calls (useful for I/O debugging)
        strace -c -p $(pidof willeq) -e trace=read,write,openat,ioctl
        ;;
    futex)
        # Trace futex calls to find mutex/lock contention causing stutter
        # Low overhead — only traces futex syscalls, not all syscalls
        strace -f -T -e futex -o futex_trace.txt $WILLEQ --config $CONFIG $ARGS
        echo "Trace written to futex_trace.txt"
        echo "Blocking calls >1s:"
        perl -ne 'print if /<(\d+\.\d+)>/ && $1 > 1.0' futex_trace.txt
        ;;
    syscall)
        # Broad syscall trace with timestamps — find I/O, ioctl, and futex blocking
        # Post-mortem: perl -ne 'print if /<(\d+\.\d+)>/ && $1 > 0.1' syscall_trace.txt
        strace -f -tt -T -e trace=read,write,recvfrom,sendto,ioctl,openat,mmap,poll,select,epoll_wait,futex \
               -o syscall_trace.txt $WILLEQ --config $CONFIG $ARGS > /tmp/willeq.log 2>&1
        echo "Trace written to syscall_trace.txt"
        echo "Blocking calls >1s:"
        perl -ne 'print if /<(\d+\.\d+)>/ && $1 > 1.0' syscall_trace.txt
        ;;
    memcheck)
        # Valgrind — very slow but catches memory bugs
        valgrind --tool=memcheck --leak-check=full \
                 $WILLEQ --config $CONFIG $ARGS 2>&1 | tee valgrind.txt
        ;;
esac
