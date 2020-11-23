#!/bin/bash
# Author: Michael Conard
# Purpose: Launch protocol udp instances and wait for completion

n=$1		# topology size
re='^[0-9]+$'	# numeric regex
pids=()		# PID list

# verify input shape
if ! [[ $n =~ $re ]] ; then
    echo "Usage: ./run_udp_protocol.sh <n>"
    exit
fi

# spawn instances
for i in `seq 0 $(( ${n}-1 ))` ; do
    ./protocol $i $n &
    pids[${i}]=$!
done

# wait for completion
for pid in ${pids[@]}; do
    wait $pid
done

