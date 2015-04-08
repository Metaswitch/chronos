#include "timer.h"
#include "globals.h"
#include "base.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <map>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

static uint32_t REPLICATION_FACTOR = 2u;
static int MAX_TIMERS = 4096;
static Hasher normal_hasher;

class TestTimerReplicaChoosing : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();
    cluster = {"10.0.0.1:7253", "10.0.0.2:7253", "10.0.0.3:7253", "10.0.0.4:7253"};
    new_cluster = cluster;
    new_cluster.push_back("10.0.0.100:7253");
  }

  virtual void TearDown()
  {
    Base::TearDown();
  }

  void calculate_timers_for_id(TimerID id)
  {
    std::map<std::string, uint64_t> cluster_hashes;
    old_replicas.clear();
    new_replicas.clear();
    Timer::calculate_replicas(id,
                              0u,
                              cluster_hashes,
                              cluster,
                              REPLICATION_FACTOR,
                              old_replicas,
                              extra_replicas,
                              &normal_hasher);

    Timer::calculate_replicas(id,
                              0u,
                              cluster_hashes,
                              new_cluster,
                              REPLICATION_FACTOR,
                              new_replicas,
                              extra_replicas,
                              &normal_hasher);
  }

  std::vector<std::string> cluster;
  std::vector<std::string> new_cluster;
  
  std::vector<std::string> old_replicas;
  std::vector<std::string> new_replicas;
  std::vector<std::string> extra_replicas;
};

/*****************************************************************************/
/* Class functions                                                           */
/*****************************************************************************/

TEST_F(TestTimerReplicaChoosing, ClusteringIsBalanced)
{
  std::map<std::string, uint32_t> cluster_counts;

  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    calculate_timers_for_id(id);
    cluster_counts[old_replicas[0]]++;
  }

  uint32_t expected_max = MAX_TIMERS / (cluster.size() - 1);
  uint32_t expected_min = MAX_TIMERS / (cluster.size() + 1);
  
  for (std::vector<std::string>::iterator ii = cluster.begin();
      ii != cluster.end();
      ii++)
  {
    EXPECT_GT(cluster_counts[*ii], expected_min);
    EXPECT_LT(cluster_counts[*ii], expected_max);
  }
}

TEST_F(TestTimerReplicaChoosing, MinimumTimersMovePrimary)
{
  int different = 0;
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    calculate_timers_for_id(id);
    
    if ((old_replicas[0] != new_replicas[0]))
    {
      ASSERT_EQ(old_replicas[1], new_replicas[1]);

      // Timers should only move onto the newly-added replica
      ASSERT_EQ(new_replicas[0], "10.0.0.100:7253");
      different++;
    }
  }

  //printf("%d of %d timers changed primary replica on scale-up (expected around %ld, won't accept more than %ld)\n", different, MAX_TIMERS, MAX_TIMERS / new_cluster.size(), (uint32_t)((MAX_TIMERS / new_cluster.size()) * 1.15));

  // To balance the cluster when we moved from 5 replicas to 6, approximately
  // 1/6th of existing timers should have moved. Allow a 15% difference to account for randomness.
  EXPECT_THAT(different, ::testing::Lt((MAX_TIMERS / new_cluster.size()) * 1.15));
}

TEST_F(TestTimerReplicaChoosing, MinimumTimersMoveBackup)
{
  int different = 0;
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    calculate_timers_for_id(id);

    if ((old_replicas[1] != new_replicas[1]))
    {
      ASSERT_EQ(old_replicas[0], new_replicas[0]);

      // Timers should only move onto the newly-added replica
      ASSERT_EQ(new_replicas[1], "10.0.0.100:7253");
      different++;
    }
  }

  //printf("%d of %d timers changed first backup replica on scale-up (expected around %ld, won't accept more than %ld)\n", different, MAX_TIMERS, MAX_TIMERS / new_cluster.size(), (uint32_t)((MAX_TIMERS / new_cluster.size()) * 1.15));

  // To balance the cluster when we moved from 5 replicas to 6, approximately
  // 1/6th of existing timers should have moved. Allow a 15% difference to account for randomness.
  EXPECT_THAT(different, ::testing::Lt((MAX_TIMERS / new_cluster.size()) * 1.15));
}

TEST_F(TestTimerReplicaChoosing, NoPrimaryBackupSwap)
{
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    calculate_timers_for_id(id);
    ASSERT_TRUE((old_replicas[0] != new_replicas[1]));
    ASSERT_TRUE((old_replicas[1] != new_replicas[0]));
  }
}


