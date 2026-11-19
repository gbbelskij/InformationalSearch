CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

all: build/tokenizer build/test_tokenizer

build/tokenizer: src/main.cpp src/tokenizer.h src/stats.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

build/test_tokenizer: tests/test_tokenizer.cpp tests/unittest.h src/tokenizer.h src/stats.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -Itests -o $@ tests/test_tokenizer.cpp

test: build/tokenizer build/test_tokenizer
	./build/test_tokenizer

clean:
	rm -rf build

.PHONY: all test clean
