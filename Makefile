# This uses platform.mk to perform the heavy lifting of defining pre-requisites and build flags.

ROOT := ${PWD}
TARGET := chronos
TARGET_TEST := chronos_test
TARGET_SOURCES_BUILD := src/main/main.cpp
TARGET_SOURCES_TEST := $(wildcard src/test/*.cpp)
TARGET_SOURCES := $(filter-out $(TARGET_SOURCES_BUILD) $(TARGET_SOURCES_TEST), $(wildcard src/main/*.cpp) $(wildcard src/main/**/*.cpp))
TARGET_EXTRA_OBJS_TEST :=
INCLUDE_DIR := ${ROOT}/src/include
CPPFLAGS := -pedantic -g -I${INCLUDE_DIR} -std=c++0x `curl-config --cflags`
CPPFLAGS_BUILD := -O0
CPPFLAGS_TEST := -O0 -fprofile-arcs
LDFLAGS := -lrt -lpthread `curl-config --libs` -levent
LDFLAGS_BUILD :=
LDFLAGS_TEST := -lgtest
EXTRA_CLEANS :=

.PHONY: default
default: build

include ${ROOT}/mk/platform.mk

.PHONY: test
test: build_test
	$(TARGET_BIN_TEST)

.PHONY: valgrind
valgrind: build_test
	valgrind --leak-check=full $(TARGET_BIN_TEST)
