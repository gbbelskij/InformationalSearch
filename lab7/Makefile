CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
TOK = ../lab3/src
STEM = ../lab4/src
UT = ../lab3/tests
VB = ../lab11/src

all: build/indexer build/lookup build/test_index

build/indexer: src/indexer.cpp src/index_builder.h src/common.h $(TOK)/tokenizer.h $(STEM)/porter.h $(VB)/vbyte.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(TOK) -I$(STEM) -I$(VB) -o $@ src/indexer.cpp

build/lookup: src/lookup.cpp src/index_reader.h src/common.h $(TOK)/tokenizer.h $(VB)/vbyte.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(TOK) -I$(VB) -o $@ src/lookup.cpp

build/test_index: tests/test_index.cpp src/index_builder.h src/index_reader.h src/common.h $(UT)/unittest.h $(TOK)/tokenizer.h $(STEM)/porter.h $(VB)/vbyte.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -I$(TOK) -I$(STEM) -I$(UT) -I$(VB) -o $@ tests/test_index.cpp

test: build/indexer build/lookup build/test_index
	./build/test_index

clean:
	rm -rf build

.PHONY: all test clean
