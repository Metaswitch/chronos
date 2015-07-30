# Alarms to JSON Makefile

SHELL := /bin/bash

ROOT := ${PWD}
MK_DIR := ${ROOT}/mk

TARGET := chronos_alarm_header
TARGET_TEST := chronos_alarm_header_test

TARGET_SOURCES := alarm_header.cpp \
                  json_alarms.cpp \
                  alarmdefinition.cpp

CPPFLAGS_BUILD := -O0
CPPFLAGS += -ggdb -std=c++0x

CPPFLAGS += -I${ROOT}/modules/cpp-common/include \
            -I${ROOT}/modules/rapidjson/include

# Add cpp-common/src as VPATH so build will find modules there.
VPATH = ${ROOT}/modules/cpp-common/src

.PHONY: default
default: build

include ${MK_DIR}/platform.mk

.PHONY: build
build: ${TARGET_BIN}
