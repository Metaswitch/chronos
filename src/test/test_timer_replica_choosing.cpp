#include "timer.h"
#include "globals.h"
#include "base.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <map>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TestTimerReplicaChoosing : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();
    id = (TimerID)7;
  }

  virtual void TearDown()
  {
    Base::TearDown();
  }

  TimerID id;
};

/*****************************************************************************/
/* Class functions                                                           */
/*****************************************************************************/

TEST_F(TestTimerReplicaChoosing, UnderHalfOfTimersMove)
{
  std::map<std::string, uint64_t> cluster_hashes;
  std::vector<std::string> cluster = {"A", "B", "C"};
  std::vector<std::string> cluster2 = {"A", "B", "C", "D"};

  std::vector<std::string> replicas;
  std::vector<std::string> replicas2;
  std::vector<std::string> extra_replicas;
  int different = 0;
  int max_timers = 65536;
  for (TimerID id = (TimerID)1;
       id < (TimerID)max_timers;
       id++)
  {
    replicas.clear();
    replicas2.clear();
    Timer::calculate_replicas(id,
                              0u,
                              cluster_hashes,
                              cluster,
                              2u,
                              replicas,
                              extra_replicas);

    Timer::calculate_replicas(id,
                              0u,
                              cluster_hashes,
                              cluster2,
                              2u,
                              replicas2,
                              extra_replicas);
    if ((replicas[0] != replicas2[0]) ||
        (replicas[1] != replicas2[1]))
    {
      different++;
    }
  }
  printf("%d of %d timers changed replica on scale-up\n", different, max_timers);
  EXPECT_THAT(different, ::testing::Lt(max_timers / 2));
}

TEST_F(TestTimerReplicaChoosing, NoPrimaryBackupSwap)
{
  std::map<std::string, uint64_t> cluster_hashes;
  std::vector<std::string> cluster = {"A", "B", "C"};
  std::vector<std::string> cluster2 = {"A", "B", "C", "D"};

  std::vector<std::string> replicas;
  std::vector<std::string> replicas2;
  std::vector<std::string> extra_replicas;
  int max_timers = 65536;
  for (TimerID id = (TimerID)1;
       id < (TimerID)max_timers;
       id++)
  {
    replicas.clear();
    replicas2.clear();
    Timer::calculate_replicas(id,
                              0u,
                              cluster_hashes,
                              cluster,
                              2u,
                              replicas,
                              extra_replicas);

    Timer::calculate_replicas(id,
                              0u,
                              cluster_hashes,
                              cluster2,
                              2u,
                              replicas2,
                              extra_replicas);
    ASSERT_NE(replicas[0], replicas2[1]);
    ASSERT_THAT(replicas[1], ::testing::Ne(replicas2[0]));
  }
}
