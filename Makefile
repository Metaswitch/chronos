# This uses platform.mk to perform the heavy lifting of defining pre-requisites and build flags.

ROOT := ${PWD}
TARGET := chronos
TARGET_TEST := chronos_test
TARGET_SOURCES := $(wildcard src/main/*.cpp)
TARGET_SOURCES_BUILD := 
TARGET_SOURCES_TEST :=
TARGET_EXTRA_OBJS_TEST :=
INCLUDE_DIR := ${ROOT}/src/include
CPPFLAGS := -g -I${INCLUDE_DIR} -std=c++0x
CPPFLAGS_BUILD := -O2
CPPFLAGS_TEST := -O0 -fprofile-arcs
LDFLAGS := -lrt
LDFLAGS_BUILD :=
LDFLAGS_TEST :=
EXTRA_CLEANS :=

all: build

include ${ROOT}/mk/platform.mk
