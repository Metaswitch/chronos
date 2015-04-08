#ifndef TIMER_H__
#define TIMER_H__

#include <vector>
#include <map>
#include <string>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"

typedef uint64_t TimerID;

class Hasher
{
public:
  virtual uint32_t do_hash(TimerID data, uint32_t seed);
  virtual uint32_t do_hash(std::string data, uint32_t seed);
};

class Timer
{
public:
  Timer(TimerID, uint32_t interval, uint32_t repeat_for);
  ~Timer();

  // For testing purposes.
  friend class TestTimer;

  // Returns the next time to pop in ms after epoch
  uint64_t next_pop_time();

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

  // Calculate/Guess at the replicas for this timer (using the replica hash if present)
  void calculate_replicas(uint64_t);

  static void calculate_replicas(TimerID id,
                                 uint64_t replica_hash,
                                 std::map<std::string, uint64_t> cluster_hashes,
                                 std::vector<std::string> cluster,
                                 uint32_t replication_factor,
                                 std::vector<std::string>& replicas,
                                 std::vector<std::string>& extra_replicas,
                                 Hasher* hasher);

  // Mark which replicas have been informed about the timer 
  int update_replica_tracker(int replica_index);

  // Member variables (mostly public since this is pretty much a struct with utility
  // functions, rather than a full-blown object).
  TimerID id;
  uint64_t start_time;
  uint32_t interval;
  uint32_t repeat_for;
  uint32_t sequence_number;
  std::vector<std::string> replicas;
  std::vector<std::string> extra_replicas;
  std::string callback_url;
  std::string callback_body;

private:
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
  static Timer* from_json(TimerID, uint64_t, std::string, std::string&, bool&);
  static Timer* from_json_obj(TimerID id,
                              uint64_t replica_hash,
                              std::string& error,
                              bool& replicated,
                              rapidjson::Value& doc);

  // Class variables
  static uint32_t deployment_id;
  static uint32_t instance_id;
};

#endif
