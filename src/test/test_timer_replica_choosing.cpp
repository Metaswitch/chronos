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
    std::map<std::string, uint64_t> cluster_bloom_filters;
    std::vector<uint32_t> cluster_rendezvous_hashes = __globals->generate_hashes(cluster);
    std::vector<uint32_t> new_cluster_rendezvous_hashes = __globals->generate_hashes(new_cluster);
    old_replicas.clear();
    new_replicas.clear();
    Timer::calculate_replicas(id,
                              0u,
                              cluster_bloom_filters,
                              cluster,
                              cluster_rendezvous_hashes,
                              REPLICATION_FACTOR,
                              old_replicas,
                              extra_replicas,
                              &normal_hasher);

    Timer::calculate_replicas(id,
                              0u,
                              cluster_bloom_filters,
                              new_cluster,
                              new_cluster_rendezvous_hashes,
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

class TestTimerReplicaChoosingWithCollision : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();
    cluster = {"10.0.0.1:7253", "10.0.0.2:7253", "10.0.0.3:7253", "10.0.0.4:7253"};
    new_cluster = {"10.0.0.2:7253", "10.0.0.3:7253", "10.0.0.4:7253"};
    cluster_rendezvous_hashes = {100, 99, 4, 5};
    new_cluster_rendezvous_hashes = {100, 4, 5};
  }

  virtual void TearDown()
  {
    Base::TearDown();
  }

  void calculate_timers_for_id(TimerID id)
  {
    std::map<std::string, uint64_t> cluster_bloom_filters;
    old_replicas.clear();
    new_replicas.clear();
    Timer::calculate_replicas(id,
                              0u,
                              cluster_bloom_filters,
                              cluster,
                              cluster_rendezvous_hashes,
                              REPLICATION_FACTOR,
                              old_replicas,
                              extra_replicas,
                              &normal_hasher);

    Timer::calculate_replicas(id,
                              0u,
                              cluster_bloom_filters,
                              new_cluster,
                              new_cluster_rendezvous_hashes,
                              REPLICATION_FACTOR,
                              new_replicas,
                              extra_replicas,
                              &normal_hasher);
  }

  std::vector<std::string> cluster;
  std::vector<std::string> new_cluster;
  
  std::vector<uint32_t> cluster_rendezvous_hashes;
  std::vector<uint32_t> new_cluster_rendezvous_hashes;
  
  std::vector<std::string> old_replicas;
  std::vector<std::string> new_replicas;
  std::vector<std::string> extra_replicas;
};


TEST_F(TestTimerReplicaChoosingWithCollision, ClusteringIsBalanced)
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

TEST_F(TestTimerReplicaChoosingWithCollision, MinimumTimersMovePrimary)
{
  int different = 0;
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    calculate_timers_for_id(id);
    
    if ((old_replicas[0] != new_replicas[0]))
    {
      different++;
    }
  }

  //printf("%d of %d timers changed primary replica on scale-up (expected around %ld, won't accept more than %ld)\n", different, MAX_TIMERS, MAX_TIMERS / new_cluster.size(), (uint32_t)((MAX_TIMERS / new_cluster.size()) * 1.15));

  // If we have a cluster of size N shringing to size M, we remove A, and that resolves a hash collision so B's hash changes:
  //  - B's hash is now A's former hash, so all A's timers (1/N) move to B
  //  - B's hash has disappeared, so all B's timers (1/N) get rehashed
  //  - Of those 1/N timers, M-1/M move to other nodes, and 1/M stay on B
  //  - Overall, 1/N + (M-1/MN) of the total timers movetimers move = (M/MN + M-1/MN) = 2M-1/MN
  //
  int expected_to_move = MAX_TIMERS * ((2 * new_cluster.size()) - 1) / (new_cluster.size() * cluster.size());
  EXPECT_THAT(different, ::testing::Lt(expected_to_move * 1.15));
}


TEST_F(TestTimerReplicaChoosingWithCollision, ServerHashesDontCollide)
{
  std::vector<std::string> cluster_with_duplicate = {"A", "A", "B"};
  std::vector<uint32_t> hashes = __globals->generate_hashes(cluster_with_duplicate);

  ASSERT_NE(hashes[0], hashes[1]);
}
