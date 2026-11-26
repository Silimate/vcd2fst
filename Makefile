# Use bash as the default shell
SHELL := /usr/bin/env bash

ifdef $(LC_ALL)
	undefine LC_ALL
endif

UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
NPROC = $(shell nproc)
endif
ifeq ($(UNAME), Darwin)
NPROC = $(shell sysctl -n hw.physicalcpu)
endif

ifeq ($(CPU_CORES),)
	CPU_CORES := $(NPROC)
	ifeq ($(CPU_CORES),)
		CPU_CORES := 2  # Good minimum assumption
	endif
endif

PREFIX ?= /usr/local
ADDITIONAL_CMAKE_OPTIONS ?=
export CTEST_PARALLEL_LEVEL = $(CPU_CORES)

release: run-cmake-release
	cmake --build build -j $(CPU_CORES)

debug: run-cmake-debug
	cmake --build build -j $(CPU_CORES)

run-cmake-release:
	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) -DBUILD_SHARED_LIBS=OFF $(ADDITIONAL_CMAKE_OPTIONS) -S . -B build

run-cmake-debug:
	cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$(PREFIX) -DBUILD_SHARED_LIBS=OFF $(ADDITIONAL_CMAKE_OPTIONS) -S . -B build

.PHONY: test
test:
	@set -e; scratch=$$(mktemp -d); trap 'rm -rf "$$scratch"' EXIT; \
	  $(CC) $(CPPFLAGS) $(CFLAGS) -I. test/real_precision.c fstapi.c fastlz.c lz4.c \
	     $(LDFLAGS) -lz $(LDLIBS) -o "$$scratch/real_precision"; \
	  $(CC) $(CPPFLAGS) $(CFLAGS) -DFST_HDR_FOURPACK_MAX_SIZE=4096 -I. test/hier_lz4_limit.c \
	     fstapi.c fastlz.c lz4.c $(LDFLAGS) -lz $(LDLIBS) -o "$$scratch/hier_lz4_limit"; \
	  $(CXX) $(CPPFLAGS) $(CXXFLAGS) -fpermissive -I. -Ithird_party/backward-cpp -x c++ vcd2fst.cpp \
	     fstapi.c fastlz.c lz4.c jrb.c third_party/backward-cpp/backward.cpp \
	     $(LDFLAGS) -lz $(LDLIBS) -o "$$scratch/vcd2fst"; \
	  $(CC) $(CPPFLAGS) $(CFLAGS) -I. test/vcd_ids.c fstapi.c fastlz.c lz4.c \
	     $(LDFLAGS) -lz $(LDLIBS) -o "$$scratch/vcd_ids"; \
	  cd "$$scratch" && ./real_precision && ./hier_lz4_limit && ./vcd_ids ./vcd2fst
