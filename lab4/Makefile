CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
TOK = ../lab3/src
UT = ../lab3/tests
IDX = ../lab7/src
QRY = ../lab8/src
VB = ../lab11/src

all: build/stem build/test_stemmer

build/stem: src/stem.cpp src/porter.h $(TOK)/tokenizer.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -I$(TOK) -o $@ src/stem.cpp

build/test_stemmer: tests/test_stemmer.cpp src/porter.h $(UT)/unittest.h $(TOK)/tokenizer.h $(IDX)/index_builder.h $(IDX)/index_reader.h $(QRY)/query.h $(VB)/vbyte.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc -I$(TOK) -I$(UT) -I$(IDX) -I$(QRY) -I$(VB) -o $@ tests/test_stemmer.cpp

test: build/stem build/test_stemmer
	$(MAKE) -s -C ../lab7 build/indexer
	$(MAKE) -s -C ../lab8 build/search
	./build/test_stemmer

clean:
	rm -rf build

.PHONY: all test clean
