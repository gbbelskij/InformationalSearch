CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
TOK = ../lab3/src
IDX = ../lab7/src
STEM = ../lab4/src
UT = ../lab3/tests
VB = ../lab11/src
RANK = ../lab13/src

all: build/search build/test_search

build/search: src/search.cpp src/query.h $(RANK)/rank.h $(IDX)/index_reader.h $(IDX)/common.h $(TOK)/tokenizer.h $(STEM)/porter.h $(VB)/vbyte.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -I$(TOK) -I$(IDX) -I$(STEM) -I$(VB) -I$(RANK) -o $@ src/search.cpp

build/test_search: tests/test_search.cpp src/query.h $(IDX)/index_builder.h $(IDX)/index_reader.h $(IDX)/common.h $(TOK)/tokenizer.h $(STEM)/porter.h $(UT)/unittest.h $(VB)/vbyte.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -I$(TOK) -I$(IDX) -I$(STEM) -I$(UT) -I$(VB) -o $@ tests/test_search.cpp

test: build/search build/test_search
	./build/test_search

clean:
	rm -rf build

.PHONY: all test clean
