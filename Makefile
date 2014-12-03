# This uses platform.mk to perform the heavy lifting of defining pre-requisites and build flags.

SHELL := /bin/bash

ROOT := ${PWD}
MK_DIR := ${ROOT}/mk
PREFIX ?= ${ROOT}/build/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/modules

TARGET := chronos
TARGET_TEST := chronos_test
TARGET_SOURCES_BUILD := src/main/main.cpp
TARGET_SOURCES_TEST := $(wildcard src/test/*.cpp) test_interposer.cpp fakelogger.cpp
TARGET_SOURCES := $(filter-out $(TARGET_SOURCES_BUILD) $(TARGET_SOURCES_TEST), $(wildcard src/main/*.cpp) $(wildcard src/main/**/*.cpp))
TARGET_SOURCES += log.cpp logger.cpp unique.cpp signalhandler.cpp alarm.cpp signalnames.cpp
TARGET_EXTRA_OBJS_TEST :=
INCLUDE_DIR := ${ROOT}/src/include
CPPFLAGS := -ggdb -I${INCLUDE_DIR} -I${ROOT}/modules/cpp-common/include -I${ROOT}/modules/rapidjson/include -std=c++0x -I ${INSTALL_DIR}/include -Werror
CPPFLAGS_BUILD := -O0
CPPFLAGS_TEST := -O0 -fprofile-arcs -ftest-coverage -DUNITTEST -I${ROOT}/src/test/ -I${ROOT}/modules/cpp-common/test_utils/
LDFLAGS := -L${INSTALL_DIR}/lib -lrt -lpthread -lcurl -levent -lboost_program_options -lboost_regex -lzmq -lc
LDFLAGS_BUILD :=
LDFLAGS_TEST := -lgtest -lgmock
VPATH := ${ROOT}/modules/cpp-common/src:${ROOT}/modules/cpp-common/test_utils

.PHONY: default
default: build

include ${ROOT}/mk/platform.mk

DEB_COMPONENT := chronos
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES := chronos chronos-dbg
EXTRA_CLEANS := ${ROOT}/gcov ${OBJ_DIR_TEST}/chronos.memcheck

SUBMODULES := c-ares curl

include build-infra/cw-deb.mk
include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})

.PHONY: deb
deb: build deb-only

.PHONY: build
build: ${SUBMODULES} ${TARGET_BIN}

.PHONY: test
test: ${SUBMODULES} ${TARGET_BIN_TEST}
	${TARGET_BIN_TEST}

.PHONY: debug
debug: ${TARGET_BIN_TEST}
	gdb --args ${TARGET_BIN_TEST}

.PHONY: testall
testall: $(patsubst %, %_test, ${SUBMODULES}) test

.PHONY: clean
clean: $(patsubst %, %_clean, ${SUBMODULES})
	rm -f ${TARGET_BIN}
	rm -f ${TARGET_OBJS}
	rm -f ${TARGET_OBJS_TEST}
	rm -rf ${EXTRA_CLEANS}
	rm -f $(DEPS)

.PHONY: distclean
distclean: $(patsubst %, %_distclean, ${SUBMODULES})
	rm -rf ${ROOT}/build

VG_OPTS := --leak-check=full --gen-suppressions=all
${OBJ_DIR_TEST}/chronos.memcheck: build_test
	valgrind ${VG_OPTS} --xml=yes --xml-file=${OBJ_DIR_TEST}/chronos.memcheck $(TARGET_BIN_TEST)

.PHONY: valgrind valgrind_xml
valgrind_xml: ${OBJ_DIR_TEST}/chronos.memcheck
valgrind: ${TARGET_BIN_TEST}
	valgrind ${VG_OPTS} ${TARGET_BIN_TEST}

.PHONY: coverage
coverage: ${TARGET_BIN_TEST}
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
