# Hash table
This is a hash table implementation in C++ inspired by the hash table design explained in this talk:
https://youtu.be/NH1Tta7purM

This design is meant to be CPU-friendly, and involves storing pointers to key-value pairs in a separate collection rather than directly in the main container.
Keys and values are stored contiguously in memory in 16 byte chunks (8 byte key, 8 byte value). This means when the CPU fetches one key value pair, it can fetch three more from the same cacheline for very little cost.

# How to use
Run the following command from the root directory to build:
`cmake -S . -B build && cmake --build build`
