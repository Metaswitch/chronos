/**
 * @file mock_replicator.h
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCK_REPLICATOR_H__
#define MOCK_REPLICATOR_H__

#include "replicator.h"

#include <gmock/gmock.h>

class MockReplicator : public Replicator
{
public:
  MockReplicator() : Replicator(NULL, NULL) {}

  MOCK_METHOD1(replicate, void(Timer*));
  MOCK_METHOD2(replicate_timer_to_node, void(Timer*, std::string));
};

#endif
