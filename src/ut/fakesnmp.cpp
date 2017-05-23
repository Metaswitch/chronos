/**
 * @file fakesnmp.cpp Fake SNMP infrastructure for UT.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "snmp_internal/snmp_includes.h"
#include "fakesnmp.hpp"

namespace SNMP
{

InfiniteTimerCountTable* InfiniteTimerCountTable::create(std::string name, std::string oid)
{
  return new FakeInfiniteTimerCountTable();
};

CounterTable* CounterTable::create(std::string name, std::string oid)
{
  return new FakeCounterTable();
};

ContinuousIncrementTable* ContinuousIncrementTable::create(std::string name, std::string oid)
{
  return new FakeContinuousIncrementTable();
};
}
