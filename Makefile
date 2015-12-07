TARGETS := chronos
TEST_TARGETS := chronos_test

COMMON_SOURCES := chronos_internal_connection.cpp handlers.cpp replicator.cpp timer_handler.cpp globals.cpp http_callback.cpp timer.cpp timer_store.cpp log.cpp logger.cpp unique.cpp signalhandler.cpp alarm.cpp httpstack.cpp httpstack_utils.cpp accesslogger.cpp utils.cpp health_checker.cpp exception_handler.cpp httpconnection.cpp statistic.cpp baseresolver.cpp dnscachedresolver.cpp dnsparser.cpp zmq_lvc.cpp httpresolver.cpp counter.cpp snmp_scalar.cpp snmp_agent.cpp snmp_row.cpp timer_counter.cpp MurmurHash3.cpp
chronos_SOURCES := ${COMMON_SOURCES} main.cpp snmp_infinite_timer_count_table.cpp snmp_infinite_scalar_table.cpp snmp_counter_table.cpp snmp_continuous_increment_table.cpp snmp_infinite_base_table.cpp load_monitor.cpp
chronos_test_SOURCES := ${COMMON_SOURCES} base.cpp fakesnmp.cpp test_globals.cpp test_handler.cpp test_main.cpp test_timer.cpp test_timer_handler.cpp test_timer_replica_choosing.cpp test_timer_store.cpp timer_helper.cpp test_interposer.cpp fakelogger.cpp mock_sas.cpp fakecurl.cpp pthread_cond_var_helper.cpp mock_increment_table.cpp mock_infinite_table.cpp mock_scalar_table.cpp

COMMON_CPPFLAGS := -Isrc/include \
                   -Imodules/cpp-common/include \
                   -Imodules/rapidjson/include \
                   -Imodules/sas-client/include \
                   -Ibuild/usr/include
chronos_CPPFLAGS := ${COMMON_CPPFLAGS}
chronos_test_CPPFLAGS := ${COMMON_CPPFLAGS} -Imodules/cpp-common/test_utils -Isrc/test

COMMON_LDFLAGS := -Lbuild/usr/lib -lrt -lpthread -lcurl -levent -lboost_program_options -lboost_regex -lzmq -lc -lboost_filesystem -lboost_system -levhtp -levent_pthreads -lcares $(shell net-snmp-config --netsnmp-agent-libs)

chronos_LDFLAGS := ${COMMON_LDFLAGS} -lsas -lz
chronos_test_LDFLAGS := ${COMMON_LDFLAGS} -ldl

VPATH := modules/cpp-common/src modules/cpp-common/test_utils src/main src/test src/main/murmur

BUILD_DIR := build
GMOCK_DIR := modules/gmock
GCOVR_DIR := modules/gcovr

include build-infra/cpp.mk

.PHONY: default
default: build

DEB_COMPONENT := chronos
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES := chronos chronos-dbg

MODULE_DIR := $(shell pwd)/modules
INSTALL_DIR := build/usr
LIB_DIR := ${INSTALL_DIR}/lib
SUBMODULES := c-ares curl libevhtp sas-client cpp-common

include build-infra/cw-deb.mk
include $(patsubst %, mk/%.mk, ${SUBMODULES})

.PHONY : submodules
submodules : ${BUILD_DIR}/.submodules_built

${BUILD_DIR}/.submodules_built : .gitmodules
	${MAKE} ${SUBMODULES}
	touch $@

.PHONY: deb
deb: chronos
	${MAKE} deb-only

ROOT := $(shell pwd)
include modules/cpp-common/makefiles/alarm-utils.mk

${chronos_OBJECT_DIR}/main.o : build/usr/include/chronos_alarmdefinition.h
build/usr/include/chronos_alarmdefinition.h : build/bin/alarm_header usr/share/clearwater/infrastructure/alarms/chronos_alarms.json
	$< -j "usr/share/clearwater/infrastructure/alarms/chronos_alarms.json" -n "chronos"
	mv chronos_alarmdefinition.h $@

.PHONY: resync_test
resync_test: build/bin/chronos
	./scripts/chronos_resync.py
