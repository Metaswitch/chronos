# This uses platform.mk to perform the heavy lifting of defining pre-requisites and build flags.

SHELL := /bin/bash

ROOT := ${PWD}
TARGET := chronos
TARGET_TEST := chronos_test
TARGET_SOURCES_BUILD := src/main/main.cpp
TARGET_SOURCES_TEST := $(wildcard src/test/*.cpp)
TARGET_SOURCES := $(filter-out $(TARGET_SOURCES_BUILD) $(TARGET_SOURCES_TEST), $(wildcard src/main/*.cpp) $(wildcard src/main/**/*.cpp))
TARGET_EXTRA_OBJS_TEST :=
INCLUDE_DIR := ${ROOT}/src/include
CPPFLAGS := -pedantic -g -I${INCLUDE_DIR} -std=c++0x `curl-config --cflags` -Werror
CPPFLAGS_BUILD := -O0
CPPFLAGS_TEST := -O0 -fprofile-arcs -ftest-coverage -DUNITTEST -I${ROOT}/src/test/
LDFLAGS := -lrt -lpthread `curl-config --libs` -levent
LDFLAGS_BUILD :=
LDFLAGS_TEST := -lgtest -lgmock
EXTRA_CLEANS := gcov

.PHONY: default
default: build

include ${ROOT}/mk/platform.mk

DEB_COMPONENT := chronos
DEB_MAJOR_VERSION := 1.0
DEB_NAMES := chronos chronos-dbg

include build-infra/cw-deb.mk

.PHONY: test
test: build_test
	${TARGET_BIN_TEST}

.PHONY: valgrind
valgrind: build_test
	valgrind --leak-check=full $(TARGET_BIN_TEST)

.PHONY: coverage
coverage: build_test
	-rm ${OBJ_DIR_TEST}/src/main/*.gcda 2> /dev/null
	@mkdir -p gcov
	${TARGET_BIN_TEST}
	gcov -o ${OBJ_DIR_TEST}/src/main/ ${OBJ_DIR_TEST}/src/main/*.o > gcov/synopsis
	@for gcov in *.gcov;                                                         \
	do                                                                           \
	  source=$$(basename $$gcov .gcov);                                          \
		found=$$(find src -name $$source | wc -l);                                 \
	  if [[ $$found != 1 ]];                                                     \
	  then                                                                       \
	    rm $$gcov;                                                               \
	  fi                                                                         \
	done
	@mv *.gcov gcov/
