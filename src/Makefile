TARGETS := chronos
TEST_TARGETS := chronos_test

COMMON_SOURCES := chronos_internal_connection.cpp \
                  handlers.cpp \
                  replicator.cpp \
                  timer_handler.cpp \
                  globals.cpp \
                  http_callback.cpp \
                  timer.cpp \
                  timer_store.cpp \
                  timer_heap.cpp \
                  log.cpp \
                  logger.cpp \
                  unique.cpp \
                  signalhandler.cpp \
                  alarm.cpp \
                  base_communication_monitor.cpp \
                  communicationmonitor.cpp \
                  httpstack.cpp \
                  httpstack_utils.cpp \
                  accesslogger.cpp \
                  utils.cpp \
                  health_checker.cpp \
                  exception_handler.cpp \
                  http_connection_pool.cpp \
                  httpclient.cpp \
                  http_request.cpp \
                  statistic.cpp \
                  baseresolver.cpp \
                  dnscachedresolver.cpp \
                  dnsparser.cpp \
                  zmq_lvc.cpp \
                  a_record_resolver.cpp \
                  counter.cpp \
                  snmp_scalar.cpp \
                  snmp_agent.cpp \
                  snmp_row.cpp \
                  timer_counter.cpp \
                  MurmurHash3.cpp \
                  chronos_gr_connection.cpp \
                  gr_replicator.cpp

chronos_SOURCES := ${COMMON_SOURCES} \
                   main.cpp \
                   snmp_infinite_timer_count_table.cpp \
                   snmp_infinite_scalar_table.cpp \
                   snmp_counter_table.cpp \
                   snmp_continuous_increment_table.cpp \
                   snmp_infinite_base_table.cpp \
                   load_monitor.cpp

chronos_test_SOURCES := ${COMMON_SOURCES} \
                        base.cpp \
                        fakesnmp.cpp \
                        test_globals.cpp \
                        test_handler.cpp \
                        test_main.cpp \
                        test_timer.cpp \
                        test_timer_handler.cpp \
                        test_timer_replica_choosing.cpp \
                        test_timer_store.cpp \
                        timer_helper.cpp \
                        test_interposer.cpp \
                        test_chronos_internal_connection.cpp \
                        test_replicator.cpp \
                        test_gr_replicator.cpp \
                        test_http_callback.cpp \
                        fakelogger.cpp \
                        mock_sas.cpp \
                        mock_http_request.cpp \
                        mockhttpstack.cpp \
                        fakecurl.cpp \
                        pthread_cond_var_helper.cpp \
                        mock_increment_table.cpp \
                        mock_infinite_table.cpp \
                        mock_infinite_scalar_table.cpp \
                        curl_interposer.cpp

COMMON_CPPFLAGS := -I../include \
                   -I../modules/cpp-common/include \
                   -I../modules/rapidjson/include \
                   -I../modules/sas-client/include \
                   -I../usr/include
chronos_CPPFLAGS := ${COMMON_CPPFLAGS}
chronos_test_CPPFLAGS := ${COMMON_CPPFLAGS} -I../modules/cpp-common/test_utils -Iut -DGTEST_USE_OWN_TR1_TUPLE=0

COMMON_LDFLAGS := -L../usr/lib -lrt -lpthread -lcurl -levent -lboost_program_options -lboost_regex -lzmq -lc -lboost_filesystem -lboost_system -levhtp -levent_pthreads -lcares $(shell net-snmp-config --netsnmp-agent-libs)

# Only use the real SAS library in the production build.
chronos_LDFLAGS := ${COMMON_LDFLAGS} -lsas -lz
chronos_test_LDFLAGS := ${COMMON_LDFLAGS} -ldl

# Add modules/cpp-common/src as a VPATH to pull in required common modules
VPATH := ../modules/cpp-common/src ../modules/cpp-common/test_utils ut murmur

include ../build-infra/cpp.mk

# Alarm definition generation rules
ROOT := ..
MODULE_DIR := ${ROOT}/modules
include ../modules/cpp-common/makefiles/alarm-utils.mk

../usr/include/chronos_alarmdefinition.h : ../build/bin/alarm_header ../chronos.root/usr/share/clearwater/infrastructure/alarms/chronos_alarms.json
	$< -j "../chronos.root/usr/share/clearwater/infrastructure/alarms/chronos_alarms.json" -n "chronos"
	mv chronos_alarmdefinition.h $@
${chronos_OBJECT_DIR}/main.o : ../usr/include/chronos_alarmdefinition.h
