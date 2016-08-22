/**
 * @file timer.h
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
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

  // Calculate the site list for this timer.
  void calculate_sites();

  // Mark which replicas have been informed about the timer
  int update_replica_tracker(int replica_index);

  // Return whether a particular replica has been informed about a timer
  bool has_replica_been_informed(int replica_index);

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
  // number and interval period
  uint32_t delay_from_sequence_position() const;

  uint32_t _replication_factor;

  // The replica tracker is used to track which replicas need to be informed
  // if the replica is being moved off the current node (e.g. during scale
  // down). Each bit corresponds to a replica in the timer's replica list,
  // where the primary replica corresponds to the least significant bit,
  // the second replica to the next least significant bit, and so on...
  uint32_t _replica_tracker;

  // Class functions
public:
  static TimerID generate_timer_id();
  static Timer* create_tombstone(TimerID, uint64_t);
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
};

#endif
