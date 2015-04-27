# This uses platform.mk to perform the heavy lifting of defining pre-requisites and build flags.

SHELL := /bin/bash

ROOT := ${PWD}
MK_DIR := ${ROOT}/mk
PREFIX ?= ${ROOT}/build/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/modules

GTEST_DIR := $(ROOT)/modules/gmock/gtest
GMOCK_DIR := $(ROOT)/modules/gmock

TARGET := chronos
TARGET_TEST := chronos_test
TARGET_SOURCES_BUILD := src/main/main.cpp
TARGET_SOURCES_TEST := $(wildcard src/test/*.cpp) test_interposer.cpp fakelogger.cpp mock_sas.cpp fakecurl.cpp
TARGET_SOURCES := $(filter-out $(TARGET_SOURCES_BUILD) $(TARGET_SOURCES_TEST), $(wildcard src/main/*.cpp) $(wildcard src/main/**/*.cpp))
TARGET_SOURCES += log.cpp logger.cpp unique.cpp signalhandler.cpp alarm.cpp httpstack.cpp httpstack_utils.cpp accesslogger.cpp utils.cpp health_checker.cpp exception_handler.cpp httpconnection.cpp statistic.cpp baseresolver.cpp dnscachedresolver.cpp dnsparser.cpp zmq_lvc.cpp httpresolver.cpp counter.cpp
TARGET_EXTRA_OBJS_TEST := gmock-all.o gtest-all.o
INCLUDE_DIR := ${ROOT}/src/include
LIB_DIR := ${INSTALL_DIR}/lib
CPPFLAGS := -ggdb -I${INCLUDE_DIR} -I${ROOT}/modules/cpp-common/include -I${ROOT}/modules/rapidjson/include -I${ROOT}/modules/sas-client/include -std=c++0x -I${INSTALL_DIR}/include -Werror -DUNIT_TEST 
CPPFLAGS_BUILD := -O0
CPPFLAGS_TEST := -O0 -fprofile-arcs -ftest-coverage -DUNITTEST -I${ROOT}/src/test/ -I${ROOT}/modules/cpp-common/test_utils/ -fno-access-control -I$(GTEST_DIR)/include -I$(GMOCK_DIR)/include
LDFLAGS := -L${INSTALL_DIR}/lib -lrt -lpthread -lcurl -levent -lboost_program_options -lboost_regex -lzmq -lc -lboost_filesystem -lboost_system -levhtp \
           -levent_pthreads -lcares
LDFLAGS_BUILD := -lsas
LDFLAGS_TEST := -ldl

TEST_XML = $(TEST_OUT_DIR)/test_detail_$(TARGET_TEST).xml
COVERAGE_XML = $(TEST_OUT_DIR)/coverage_$(TARGET_TEST).xml
COVERAGE_LIST_TMP = $(TEST_OUT_DIR)/coverage_list_tmp
COVERAGE_LIST = $(TEST_OUT_DIR)/coverage_list
COVERAGE_MASTER_LIST = src/test/coverage-not-yet

EXTRA_CLEANS += $(TEST_XML) \
                $(COVERAGE_XML) \
                $(OBJ_DIR_TEST)/*.gcno \
                $(OBJ_DIR_TEST)/*.gcda \
                *.gcov \
		${OBJ_DIR_TEST}/chronos.memcheck

# Now the GMock / GTest boilerplate.
GTEST_HEADERS := $(GTEST_DIR)/include/gtest/*.h \
                 $(GTEST_DIR)/include/gtest/internal/*.h
GMOCK_HEADERS := $(GMOCK_DIR)/include/gmock/*.h \
                 $(GMOCK_DIR)/include/gmock/internal/*.h \
                 $(GTEST_HEADERS)

GTEST_SRCS_ := $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)
GMOCK_SRCS_ := $(GMOCK_DIR)/src/*.cc $(GMOCK_HEADERS)
# End of boilerplate

VPATH := ${ROOT}/modules/cpp-common/src:${ROOT}/modules/cpp-common/test_utils

COVERAGEFLAGS = $(OBJ_DIR_TEST) --object-directory=$(shell pwd) --root=${ROOT} \
                --exclude='(^src/include/|^modules/gmock/|^modules/sas-client/|^modules/rapidjson/|^modules/cpp-common/include/|^modules/cpp-common/test_utils/|^src/test/|^usr/)' \
                --sort-percentage

.PHONY: default
default: build

include ${ROOT}/mk/platform.mk

DEB_COMPONENT := chronos
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES := chronos chronos-dbg

SUBMODULES := c-ares curl libevhtp sas-client cpp-common

include build-infra/cw-deb.mk
include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})

.PHONY: deb
deb: build deb-only

.PHONY: build
build: ${SUBMODULES} ${TARGET_BIN}

# Define JUSTTEST=<testname> to test just that test.  Easier than
# passing the --gtest_filter in EXTRA_TEST_ARGS.
ifdef JUSTTEST
  EXTRA_TEST_ARGS ?= --gtest_filter=$(JUSTTEST)
endif

.PHONY: test 
test: ${SUBMODULES} ${TARGET_BIN_TEST} run_test coverage-check

.PHONY: run_test
run_test: ${TARGET_BIN_TEST}
	rm -f $(TEST_XML)
	rm -f $(OBJ_DIR_TEST)/*.gcda
	$(TARGET_BIN_TEST) $(EXTRA_TEST_ARGS) --gtest_output=xml:$(TEST_XML)

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

.PHONY: resync_test
resync_test: build
	./scripts/chronos_resync.py

VG_OPTS := --leak-check=full --gen-suppressions=all
${OBJ_DIR_TEST}/chronos.memcheck: build_test
	valgrind ${VG_OPTS} --xml=yes --xml-file=${OBJ_DIR_TEST}/chronos.memcheck $(TARGET_BIN_TEST)

.PHONY: valgrind valgrind_xml
valgrind_xml: ${OBJ_DIR_TEST}/chronos.memcheck
valgrind: ${TARGET_BIN_TEST}
	valgrind ${VG_OPTS} ${TARGET_BIN_TEST}

.PHONY: coverage
coverage: | run_test
	$(GCOVR) $(COVERAGEFLAGS) --xml > $(COVERAGE_XML)

# Check that we have 100% coverage of all files except those that we
# have declared we're being relaxed on.  In particular, all new files
# must have 100% coverage or be added to $(COVERAGE_MASTER_LIST).
# The string "Marking build unstable" is recognised by the CI scripts
# and if it is found the build is marked unstable.
.PHONY: coverage-check
coverage-check: | coverage
	@xmllint --xpath '//class[@line-rate!="1.0"]/@filename' $(COVERAGE_XML) \
		| tr ' ' '\n' \
                | grep filename= \
                | cut -d\" -f2 \
                | sort > $(COVERAGE_LIST_TMP)
	@sort $(COVERAGE_MASTER_LIST) | comm -23 $(COVERAGE_LIST_TMP) - > $(COVERAGE_LIST)
	@if grep -q ^ $(COVERAGE_LIST) ; then \
                echo "Error: some files unexpectedly have less than 100% code coverage:" ; \
                cat $(COVERAGE_LIST) ; \
                /bin/false ; \
                echo "Marking build unstable." ; \
        fi

# Get quick coverage data at the command line. Add --branches to get branch info
# instead of line info in report.  *.gcov files generated in current directory
# if you need to see full detail.
.PHONY: coverage_raw
coverage_raw: | run_test
	$(GCOVR) $(COVERAGEFLAGS) --keep

# Build rules for GMock/GTest library.
$(OBJ_DIR_TEST)/gtest-all.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -I$(GMOCK_DIR) -I$(GMOCK_DIR)/include \
            -DGTEST_USE_OWN_TR1_TUPLE=0 -c $(GTEST_DIR)/src/gtest-all.cc -o $@

$(OBJ_DIR_TEST)/gmock-all.o : $(GMOCK_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -I$(GMOCK_DIR) -I$(GMOCK_DIR)/include \
            -c $(GMOCK_DIR)/src/gmock-all.cc -o $@

