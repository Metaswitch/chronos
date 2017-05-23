/**
 * @file test_timer_replica_choosing.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "timer.h"
#include "globals.h"
#include "base.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <map>

using testing::Types;

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

static uint32_t REPLICATION_FACTOR = 2u;
static int MAX_TIMERS = 768;
static Hasher normal_hasher;

class WithReplicas {
  static void set_timer_id_format() {
    Globals::TimerIDFormat timer_id_format = Globals::TimerIDFormat::WITH_REPLICAS;
    __globals->set_timer_id_format(timer_id_format);
  }
};

class WithoutReplicas {
  static void set_timer_id_format() {
    Globals::TimerIDFormat timer_id_format = Globals::TimerIDFormat::WITHOUT_REPLICAS;
    __globals->set_timer_id_format(timer_id_format);
  }
};

template <class T>
class TestTimerReplicaChoosing : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();
    T::set_timer_id_format();
    old_cluster = {"10.0.0.1:7253", "10.0.0.2:7253", "10.0.0.3:7253", "10.0.0.4:7253"};
    new_cluster = old_cluster;
    new_cluster.push_back("10.0.0.100:7253");
    
    old_cluster_rendezvous_hashes = __globals->generate_hashes(old_cluster);
    new_cluster_rendezvous_hashes = __globals->generate_hashes(new_cluster);
  }

  virtual void TearDown()
  {
    Globals::TimerIDFormat timer_id_format = __globals->default_id_format();
    __globals->set_timer_id_format(timer_id_format);
    Base::TearDown();
  }

  void calculate_timers_for_id(TimerID id)
  {
    std::map<std::string, uint64_t> cluster_bloom_filters;
    old_replicas.clear();
    new_replicas.clear();

    Globals::TimerIDFormat timer_id_format;
    __globals->get_timer_id_format(timer_id_format);

    if (timer_id_format == Globals::TimerIDFormat::WITH_REPLICAS)
    {
      Timer::calculate_replicas(id,
                                0u,
                                cluster_bloom_filters,
                                old_cluster,
                                old_cluster_rendezvous_hashes,
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
    else
    {
      Timer::calculate_replicas(id,
                                old_cluster,
                                old_cluster_rendezvous_hashes,
                                old_cluster,
                                old_cluster_rendezvous_hashes,
                                REPLICATION_FACTOR,
                                old_replicas,
                                extra_replicas,
                                &normal_hasher);

      Timer::calculate_replicas(id,
                                new_cluster,
                                new_cluster_rendezvous_hashes,
                                old_cluster,
                                old_cluster_rendezvous_hashes,
                                REPLICATION_FACTOR,
                                new_replicas,
                                extra_replicas,
                                &normal_hasher);
    }
  }

  std::vector<std::string> old_cluster;
  std::vector<std::string> new_cluster;
  
  std::vector<uint32_t> old_cluster_rendezvous_hashes;
  std::vector<uint32_t> new_cluster_rendezvous_hashes;

  std::vector<std::string> old_replicas;
  std::vector<std::string> new_replicas;
  std::vector<std::string> extra_replicas;
};

typedef ::testing::Types<WithReplicas, WithoutReplicas> TimerIDFormatTypes;
TYPED_TEST_CASE(TestTimerReplicaChoosing, TimerIDFormatTypes);

// The algorithm used to choose replicas should balance timers fairly - e.g. in
// cluster of 10 nodes, each should have 10% of the timers.
TYPED_TEST(TestTimerReplicaChoosing, ClusteringIsBalanced)
{
  std::map<std::string, uint32_t> cluster_counts;

  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    TestFixture::calculate_timers_for_id(id);
    cluster_counts[TestFixture::old_replicas[0]]++;
  }

  uint32_t expected_max = MAX_TIMERS / (TestFixture::old_cluster.size() - 1);
  uint32_t expected_min = MAX_TIMERS / (TestFixture::old_cluster.size() + 1);
  
  for (std::vector<std::string>::iterator ii = TestFixture::old_cluster.begin();
      ii != TestFixture::old_cluster.end();
      ii++)
  {
    EXPECT_GT(cluster_counts[*ii], expected_min);
    EXPECT_LT(cluster_counts[*ii], expected_max);
  }
}

// The algorithm used to choose replicas should move as few timers as possible
// on scale-up - if we have "A, B, C" and scale up to "A, B, C, D", D needs to end
// up with 25% of timers - so only 25% of timers should move primary, and only
// to D (e.g. no moves from B to C).
TYPED_TEST(TestTimerReplicaChoosing, MinimumTimersMovePrimary)
{
  int different = 0;
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    TestFixture::calculate_timers_for_id(id);
    
    if ((TestFixture::old_replicas[0] != TestFixture::new_replicas[0]))
    {
      ASSERT_EQ(TestFixture::old_replicas[1], TestFixture::new_replicas[1]);

      // Timers should only move onto the newly-added replica
      ASSERT_EQ(TestFixture::new_replicas[0], "10.0.0.100:7253");
      different++;
    }
  }

  // To balance the cluster when we moved from N replicas to N+1, approximately
  // 1/N+1th of existing timers should have moved. Allow a 10% difference to account for randomness.
  EXPECT_THAT(different, ::testing::Lt((MAX_TIMERS / TestFixture::new_cluster.size()) * 1.1));
}

// Same logic as previous test, but checking backup instead of primary.
TYPED_TEST(TestTimerReplicaChoosing, MinimumTimersMoveBackup)
{
  int different = 0;
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    TestFixture::calculate_timers_for_id(id);

    if ((TestFixture::old_replicas[1] != TestFixture::new_replicas[1]))
    {
      ASSERT_EQ(TestFixture::old_replicas[0], TestFixture::new_replicas[0]);

      // Timers should only move onto the newly-added replica
      ASSERT_EQ(TestFixture::new_replicas[1], "10.0.0.100:7253");
      different++;
    }
  }


  // To balance the cluster when we moved from N replicas to N+1, approximately
  // 1/N+1th of existing timers should have moved. Allow a 5% difference to account for randomness.
  EXPECT_THAT(different, ::testing::Lt((MAX_TIMERS / TestFixture::new_cluster.size()) * 1.05));
}

// The algorithm used to distribute timers should ensure that no primary nodes
// become backup nodes on scale-up, and that no backup nodes become primary.
TYPED_TEST(TestTimerReplicaChoosing, NoPrimaryBackupSwap)
{
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    TestFixture::calculate_timers_for_id(id);
    ASSERT_TRUE((TestFixture::old_replicas[0] != TestFixture::new_replicas[1]));
    ASSERT_TRUE((TestFixture::old_replicas[1] != TestFixture::new_replicas[0]));
  }
}

// Test behaviour when the hashes of two servers collide, and the collision is resolved by removing one.
template <class T>
class TestTimerReplicaChoosingWithCollision : public TestTimerReplicaChoosing<T>
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();
    T::set_timer_id_format();
    TestTimerReplicaChoosing<T>::old_cluster = {"10.0.0.1:7253", "10.0.0.2:7253", "10.0.0.3:7253", "10.0.0.4:7253"};
    TestTimerReplicaChoosing<T>::new_cluster = {"10.0.0.2:7253", "10.0.0.3:7253", "10.0.0.4:7253"};

    // Emulate behaviour if "10.0.0.1:7253" and "10.0.0.2:7253" both hashed to 100.
    //  - On the initial collision, "10.0.0.2:7253"'s hash would be decremented to 99.
    //  - Once "10.0.0.1:7253" was removed, "10.0.0.2:7253"'s hash would no longer need
    //    to be decremented, and would change to 100.
    TestTimerReplicaChoosing<T>::old_cluster_rendezvous_hashes = {100, 99, 4, 5};
    TestTimerReplicaChoosing<T>::new_cluster_rendezvous_hashes = {100, 4, 5};
  }

  virtual void TearDown()
  {
    Globals::TimerIDFormat timer_id_format = __globals->default_id_format();
    __globals->set_timer_id_format(timer_id_format);
    Base::TearDown();
  }
};

TYPED_TEST_CASE(TestTimerReplicaChoosingWithCollision, TimerIDFormatTypes);

// The collision algorithm should keep timers balanced across all the nodes.
TYPED_TEST(TestTimerReplicaChoosingWithCollision, ClusteringIsBalanced)
{
  std::map<std::string, uint32_t> cluster_counts;

  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    TestFixture::calculate_timers_for_id(id);
    cluster_counts[TestFixture::old_replicas[0]]++;
  }

  uint32_t expected_max = MAX_TIMERS / (TestFixture::old_cluster.size() - 1);
  uint32_t expected_min = MAX_TIMERS / (TestFixture::old_cluster.size() + 1);
  
  for (std::vector<std::string>::iterator ii = TestFixture::old_cluster.begin();
      ii != TestFixture::old_cluster.end();
      ii++)
  {
    EXPECT_GT(cluster_counts[*ii], expected_min);
    EXPECT_LT(cluster_counts[*ii], expected_max);
  }
}

// When a collision is resolved, slightly more timers than normal will move.
// Check that the number that move does not exceed the expectations we've
// calculated.

TYPED_TEST(TestTimerReplicaChoosingWithCollision, MinimumTimersMovePrimary)
{
  int different = 0;
  for (TimerID id = (TimerID)0;
       id < (TimerID)MAX_TIMERS;
       id++)
  {
    TestFixture::calculate_timers_for_id(id);
    
    if ((TestFixture::old_replicas[0] != TestFixture::new_replicas[0]))
    {

      // Timers should only move off the removed server to the server whose
      // hash has replaced it, or off the server whose hash has changed to any
      // remaining server
      
      ASSERT_TRUE(((TestFixture::old_replicas[0] == "10.0.0.1:7253") && (TestFixture::new_replicas[0] == "10.0.0.2:7253")) ||
                  ((TestFixture::old_replicas[0] == "10.0.0.2:7253") && (TestFixture::new_replicas[0] != "10.0.0.1:7253")));
      different++;
    }
  }

  // If we have a cluster of size N shringing to size M, we remove A, and that resolves a hash collision so B's hash changes:
  //  - B's hash is now A's former hash, so all A's timers (1/N) move to B
  //  - B's hash has disappeared, so all B's timers (1/N) get rehashed
  //  - Of those 1/N timers, M-1/M move to other nodes, and 1/M stay on B
  //  - Overall, 1/N + (M-1/MN) of the total timers movetimers move = (M/MN + M-1/MN) = 2M-1/MN
  int expected_to_move = MAX_TIMERS * ((2 * TestFixture::new_cluster.size()) - 1) / (TestFixture::new_cluster.size() * TestFixture::old_cluster.size());

  // Allow a 5% deviation from the expected value (as our subset of timers won't necessarily be perfectly distributed).
  EXPECT_THAT(different, ::testing::Lt(expected_to_move * 1.05));
}


TYPED_TEST(TestTimerReplicaChoosingWithCollision, ServerHashesDontCollide)
{
  std::vector<std::string> cluster_with_duplicate = {"A", "A", "B"};
  std::vector<uint32_t> hashes = __globals->generate_hashes(cluster_with_duplicate);

  ASSERT_NE(hashes[0], hashes[1]);
}
