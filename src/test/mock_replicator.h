#ifndef MOCK_REPLICATOR_H__
#define MOCK_REPLICATOR_H__

#include "replicator.h"

#include <gmock/gmock.h>

class MockReplicator : public Replicator
{
public:
  MOCK_METHOD1(replicate, void(Timer*));
};

#endif
