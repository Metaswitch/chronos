/**
 * @file timer.h
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef TIMER_H__
#define TIMER_H__

#include <vector>
#include <map>
#include <string>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "timer_heap.h"

typedef uint64_t TimerID;

// Separate class implementing the hash approach for rendezvous hashing -
// allows the hashing to be changed in UT (e.g. to force collisions).
class Hasher
{
public:
  virtual uint32_t do_hash(TimerID data, uint32_t seed);
};

class Timer : public HeapableTimer
{
public:
  Timer(TimerID, uint32_t interval_ms, uint32_t repeat_for);
  ~Timer();

  // For testing purposes.
  friend class TestTimer;

  // Returns the next time to pop in ms after epoch
  uint32_t next_pop_time() const;
  
  // Required method for use in a heap
  uint64_t get_pop_time() const;

  // Construct the URL for this timer given a hostname
  std::string url(std::string host = "");

  // Convert this timer to JSON to be sent to replicas
  std::string to_json();
  void to_json_obj(rapidjson::Writer<rapidjson::StringBuffer>* writer);

  // Check if the timer is owned by the specified node.
  bool is_local(std::string);

  // Check if this node is the last replica for the timer
  bool is_last_replica();

  // Check if a timer is a tombstone record.
  bool is_tombstone();

  // Convert this timer to its own tombstone.
  void become_tombstone();

  // Check if the timer has a matching cluster view ID
  bool is_matching_cluster_view_id(std::string cluster_view_id_to_match);

  // Calculate the replicas for this timer.
  void calculate_replicas(uint64_t replica_hash);

  // Class method for calculating replicas, for easy UT.
  static void calculate_replicas(TimerID id,
                                 std::vector<std::string> new_cluster,
                                 std::vector<uint32_t> new_cluster_rendezvous_hashes,
                                 std::vector<std::string> old_cluster,
                                 std::vector<uint32_t> old_cluster_rendezvous_hashes,
                                 uint32_t replication_factor,
                                 std::vector<std::string>& replicas,
                                 std::vector<std::string>& extra_replicas,
                                 Hasher* hasher);

  static void calculate_replicas(TimerID id,
                                 uint64_t replica_bloom_filter,
                                 std::map<std::string, uint64_t> cluster_bloom_filters,
                                 std::vector<std::string> cluster,
                                 std::vector<uint32_t> cluster_rendezvous_hashes,
                                 uint32_t replication_factor,
                                 std::vector<std::string>& replicas,
                                 std::vector<std::string>& extra_replicas,
                                 Hasher* hasher);

  // Populate the site list for this timer. Should be called when the site
  // list is empty
  void populate_sites();

  // Update the site list for a timer. Should be called when the timer has
  // just popped
  void update_sites_on_timer_pop();

  // Update the cluster information stored in the timer (replica list and
  // cluster view ID)
  void update_cluster_information();

  // Member variables (mostly public since this is pretty much a struct with utility
  // functions, rather than a full-blown object).
  TimerID id;
  uint32_t start_time_mono_ms;
  uint32_t interval_ms;
  uint32_t repeat_for;
  uint32_t sequence_number;
  std::string cluster_view_id;
  std::vector<std::string> replicas;
  std::vector<std::string> extra_replicas;
  std::vector<std::string> sites;
  std::map<std::string, uint32_t> tags;
  std::string callback_url;
  std::string callback_body;

private:
  // Work out how delayed the timer should be based on this node's position
  // in the replica list
  uint32_t delay_from_replica_position() const;

  // Work out how delayed the timer should be based on this node's position
  // in the site list
  uint32_t delay_from_site_position() const;

  // Work out how delayed the timer should be based on the timer's sequence
  // number and interval period (i.e. if this is a repeating timer)
  uint32_t delay_from_sequence_position() const;

  uint32_t _replication_factor;

  // Class functions
public:
  static TimerID generate_timer_id();
  static Timer* create_tombstone(TimerID, uint64_t, uint32_t);
  static Timer* from_json(TimerID id,
                          uint32_t replication_factor,
                          uint64_t replica_hash,
                          std::string json,
                          std::string& error,
                          bool& replicated,
                          bool& gr_replicated);
  static Timer* from_json_obj(TimerID id,
                              uint32_t replication_factor,
                              uint64_t replica_hash,
                              std::string& error,
                              bool& replicated,
                              bool& gr_replicated,
                              rapidjson::Value& doc);

  // Sort timers by their pop time
  static bool compare_timer_pop_times(Timer* t1, Timer* t2)
  {
    return (t1->next_pop_time() < t2->next_pop_time());
  }
};

#endif
