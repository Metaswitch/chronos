/**
 * @file mock_gr_replicator.h
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCK_GR_REPLICATOR_H__
#define MOCK_GR_REPLICATOR_H__

#include "gr_replicator.h"

#include <gmock/gmock.h>

class MockGRReplicator : public GRReplicator
{
public:
  MockGRReplicator() : GRReplicator(NULL, NULL) {}

  MOCK_METHOD1(replicate, void(Timer*));
};

#endif
