# TinyCFG Host Benchmark Makefile
# Requires: g++ (MinGW on Windows, or GCC/Clang on Linux/macOS)

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -DTINYCFG_HOST_BENCH
INCLUDES := -Ihost -I../src
LDFLAGS  :=

SRC_CPP  := tinycfg_bench.cpp host/arduino_shim.cpp \
            ../src/TinyCFG.cpp ../src/TinyCFGDependency.cpp

TARGET   := tinycfg_bench

.PHONY: all clean run analyze

all: $(TARGET)

$(TARGET): $(SRC_CPP) ../src/grammars/smart_world_grammar.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SRC_CPP) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) dataset.csv 500 results_raw.csv

analyze: run
	python analyze_results.py results_raw.csv

clean:
	rm -f $(TARGET) $(TARGET).exe results_raw.csv BENCHMARK_ANALYSIS.md
