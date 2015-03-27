#ifndef MOCK_REPLICATOR_H__
#define MOCK_REPLICATOR_H__

#include "replicator.h"

#include <gmock/gmock.h>

class MockReplicator : public Replicator
{
public:
  MockReplicator() : Replicator(NULL) {}

  MOCK_METHOD1(replicate, void(Timer*));
  MOCK_METHOD2(replicate_timer_to_node, void(Timer*, std::string));
};

#endif
