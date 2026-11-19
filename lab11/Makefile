CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
TOK = ../lab3/src
STEM = ../lab4/src
IDX = ../lab7/src
QRY = ../lab8/src
UT = ../lab3/tests

all: build/listbench build/test_compress

build/listbench: src/listbench.cpp src/vbyte.h $(IDX)/index_reader.h $(IDX)/common.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -I$(IDX) -o $@ src/listbench.cpp

build/test_compress: tests/test_compress.cpp src/vbyte.h $(IDX)/index_builder.h $(IDX)/index_reader.h $(IDX)/common.h $(QRY)/query.h $(TOK)/tokenizer.h $(STEM)/porter.h $(UT)/unittest.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -I$(TOK) -I$(STEM) -I$(IDX) -I$(QRY) -I$(UT) -o $@ tests/test_compress.cpp

test: build/listbench build/test_compress
	$(MAKE) -s -C ../lab7 build/indexer build/lookup
	$(MAKE) -s -C ../lab8 build/search
	./build/test_compress

clean:
	rm -rf build

.PHONY: all test clean
