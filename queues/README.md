# Queues
This directory contains two different implementations for queues in C++. Both are inspired by these talks at CppCon:

https://youtu.be/sX2nF1fW7kI
https://youtu.be/8uAW5FQtcvE

## FastQueue

## SeqLockQueue
- ring buffer
- one writer, many readers
- seqlock to ensure high read throughput