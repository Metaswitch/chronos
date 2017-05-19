/**
 * @file fakesnmp.hpp Fake SNMP infrastructure for UT.
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef FAKE_SNMP_H
#define FAKE_SNMP_H

#include "snmp_row.h"
#include "snmp_infinite_timer_count_table.h"
#include "snmp_counter_table.h"
#include "snmp_scalar.h"
#include "snmp_continuous_increment_table.h"

namespace SNMP
{

class FakeInfiniteTimerCountTable: public InfiniteTimerCountTable
{
public:
  FakeInfiniteTimerCountTable() {};
  void increment(std::string, uint32_t) {};
  void decrement(std::string, uint32_t) {};
};

class FakeCounterTable: public CounterTable
{
public:
  FakeCounterTable() {};
  void increment() {};
};

class FakeContinuousIncrementTable: public ContinuousIncrementTable
{
public:
  FakeContinuousIncrementTable() {};
  void increment(uint32_t) {};
  void decrement(uint32_t) {};
};
}

#endif
