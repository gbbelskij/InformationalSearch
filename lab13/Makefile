CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
TOK = ../lab3/src
STEM = ../lab4/src
IDX = ../lab7/src
QRY = ../lab8/src
VB = ../lab11/src
UT = ../lab3/tests

all: build/test_rank

build/test_rank: tests/test_rank.cpp src/rank.h $(QRY)/query.h $(IDX)/index_builder.h $(IDX)/index_reader.h $(IDX)/common.h $(VB)/vbyte.h $(TOK)/tokenizer.h $(STEM)/porter.h $(UT)/unittest.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -I$(TOK) -I$(STEM) -I$(IDX) -I$(QRY) -I$(VB) -I$(UT) -o $@ tests/test_rank.cpp

test: build/test_rank
	$(MAKE) -s -C ../lab7 build/indexer build/lookup
	$(MAKE) -s -C ../lab8 build/search
	./build/test_rank

clean:
	rm -rf build

.PHONY: all test clean
