CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

all: main bench

main: main.cpp
	$(CXX) $(CXXFLAGS) -o main main.cpp

bench: bench.cpp main.cpp
	$(CXX) $(CXXFLAGS) -o bench bench.cpp

run-main: main
	./main

run-bench: bench
	./bench

clean:
	rm -f main bench

.PHONY: all run-main run-bench clean
