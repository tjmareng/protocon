
# UDP Implementation of Self-Stabilizing Protocols

This directory contains a UDP-based protocol that can implement shared memory self-stabilizing protocols in a way that will (or at least seems to) recover if any of the protocol data is corrupted (before or after it's put on the wire).
Since it's UDP, we also assume that packets will be duplicated and dropped, though the duplication must *eventually* be bounded.

## Proof of Stabilization

This is left as an exercise for the reader.

Just kidding; we're working on it.
We sometimes find issues during the proof process, so consider the current code a beta version.

# Compile

Compile the udp-impl/protocol.c file using the command `gcc -o protocol protocol.c -lpthread -lrt`. You can run this program using the run_udp_protocol.sh file via `./run_udp_protocol.sh <n>` where <n> is the number of processes.
