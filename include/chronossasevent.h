/**
 * @file chronossasevent.h Chronos-specific SAS event IDs
 *
 * Copyright (C) 2017 Metaswitch Networks
 */

#ifndef CHRONOSSASEVENT_H__
#define CHRONOSSASEVENT_H__

#include "sasevent.h"

#define CHRONOS_BASE 0x8D0000

namespace SASEvent
{
  //----------------------------------------------------------------------------
  // Chronos events.
  //----------------------------------------------------------------------------
  const int HANDLE_TIMER_REQUEST = CHRONOS_BASE + 0x000000;

  const int ADD_TIMER_CLUSTER_ID = CHRONOS_BASE + 0x000010;
  const int ADD_TIMER_NEWER_IN_TIME = CHRONOS_BASE + 0x000011;
  const int DISCARD_TIMER_OLDER_IN_TIME = CHRONOS_BASE + 0x000012;
  const int ADD_TIMER_NEWER_IN_SEQUENCE = CHRONOS_BASE + 0x000013;
  const int DISCARD_TIMER_OLDER_IN_SEQUENCE = CHRONOS_BASE + 0x000014;
  const int ADD_NEW_TIMER = CHRONOS_BASE + 0x000015;

  const int POP_TIMER = CHRONOS_BASE + 0x000020;

  const int SUCCESSFUL_CALLBACK = CHRONOS_BASE + 0x000030;
  const int FAILED_CALLBACK = CHRONOS_BASE + 0x000031;

} //namespace SASEvent

#endif
