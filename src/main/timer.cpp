/**
 * @file timer.cpp
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

#include "timer.h"
#include "globals.h"
#include "murmur/MurmurHash3.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"
#include "json_parse_utils.h"
#include "utils.h"
#include "log.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <map>
#include <atomic>
#include <time.h>

uint32_t Hasher::do_hash(TimerID data, uint32_t seed)
{
  uint32_t hash;
  MurmurHash3_x86_32(&data, sizeof(TimerID), seed, &hash);
  return hash;
}

static Hasher hasher;

inline uint32_t clock_gettime_ms(int clock_id)
{
  struct timespec now;
  clock_gettime(clock_id, &now);
  uint64_t time = now.tv_sec;
  time *= 1000;
  time += now.tv_nsec / 1000000;
  return time;
}

Timer::Timer(TimerID id, uint32_t interval_ms, uint32_t repeat_for) :
  id(id),
  interval_ms(interval_ms),
  repeat_for(repeat_for),
  sequence_number(0),
  replicas(std::vector<std::string>()),
  tags(std::vector<std::string>()),
  callback_url(""),
  callback_body(""),
  _replication_factor(0),
  _replica_tracker(0)
{
  // Set the start time to now
  start_time_mono_ms = clock_gettime_ms(CLOCK_MONOTONIC);

  // Get the cluster view ID from global configuration
  std::string global_cluster_view_id;
  __globals->get_cluster_view_id(global_cluster_view_id);
  cluster_view_id = global_cluster_view_id;
}

Timer::~Timer()
{
}

// Returns the next pop time in ms.
uint32_t Timer::next_pop_time()
{
  std::string localhost;
  int replica_index = 0;
  __globals->get_cluster_local_ip(localhost);

  for (std::vector<std::string>::iterator it = replicas.begin();
                                          it != replicas.end();
                                          ++it, ++replica_index)
  {
    if (*it == localhost)
    {
      break;
    }
  }

  return start_time_mono_ms + ((sequence_number + 1) * interval_ms) + (replica_index * 2 * 1000);
}

// Create the timer's URL from a given hostname
std::string Timer::url(std::string host)
{
  std::stringstream ss;

  // Here (and below) we render the timer ID (and replica hash) as 0-padded
  // hex strings so we can parse it back out later easily.
  if (host != "")
  {
    std::string address;
    int port;
    if (!Utils::split_host_port(host, address, port))
    {
      // Just use the server as the address.
      address = host;
      __globals->get_bind_port(port);
    }

    ss << "http://" << address << ":" << port;
  }

  ss << "/timers/";
  ss << std::setfill('0') << std::setw(16) << std::hex << id;
  uint64_t hash = 0;
  std::map<std::string, uint64_t> cluster_bloom_filters;
  __globals->get_cluster_bloom_filters(cluster_bloom_filters);

  for (std::vector<std::string>::iterator it = replicas.begin();
                                          it != replicas.end();
                                          ++it)
  {
    hash |= cluster_bloom_filters[*it];
  }
  ss << std::setfill('0') << std::setw(16) << std::hex << hash;

  return ss.str();
}

// Render the timer as JSON to be used in an HTTP request body.
// The JSON should take the form:
// {
//     "timing": {
//         "start-time": UInt64, // Wall-time in milliseconds - Deprecated in favor of "start-time-delta"
//         "start-time-delta": Int32, // Millisecond offset from current time
//         "sequence-number": Int,
//         "interval": Int,
//         "repeat-for": Int
//     },
//     "callback": {
//         "http": {
//             "uri": "string",
//             "opaque": "string"
//         }
//     },
//     "reliability": {
//         "cluster-view-id": "string",
//         "replicas": [
//             <comma separated "string"s>
//         ]
//     }
//     "statistics": {
//         "tags": [
//             <comma separated "string"s>
//         ]
//     }
// }
std::string Timer::to_json()
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  to_json_obj(&writer);

  std::string body = sb.GetString();
  TRC_DEBUG("Built replication body: %s", body.c_str());

  return body;
}

void Timer::to_json_obj(rapidjson::Writer<rapidjson::StringBuffer>* writer)
{
  writer->StartObject();
  {
    writer->String("timing");
    writer->StartObject();
    {
      uint32_t realtime = clock_gettime_ms(CLOCK_REALTIME);
      uint32_t monotime = clock_gettime_ms(CLOCK_MONOTONIC);
      int32_t delta = start_time_mono_ms - monotime;
      writer->String("start-time");
      writer->Int64(realtime + delta);
      writer->String("start-time-delta");
      writer->Int64(delta);
      writer->String("sequence-number");
      writer->Int(sequence_number);
      writer->String("interval");
      writer->Int(interval_ms/1000);
      writer->String("repeat-for");
      writer->Int(repeat_for/1000);
    }
    writer->EndObject();

    writer->String("callback");
    writer->StartObject();
    {
      writer->String("http");
      writer->StartObject();
      {
        writer->String("uri");
        writer->String(callback_url.c_str());
        writer->String("opaque");
        writer->String(callback_body.c_str());
      }
      writer->EndObject();
    }
    writer->EndObject();

    writer->String("reliability");
    writer->StartObject();
    {
      writer->String("cluster-view-id");
      writer->String(cluster_view_id.c_str());
      writer->String("replicas");
      writer->StartArray();
      {
        for (std::vector<std::string>::iterator it = replicas.begin();
                                                it != replicas.end();
                                                ++it)
        {
          writer->String((*it).c_str());
        }
      }
      writer->EndArray();
    }
    writer->EndObject();

    writer->String("statistics");
    writer->StartObject();
    {
      writer->String("tags");
      writer->StartArray();
      {
        for (std::vector<std::string>::iterator it = tags.begin();
                                                it != tags.end();
                                                ++it)
        {
          writer->String((*it).c_str());
        }
      }
      writer->EndArray();
    }
    writer->EndObject();
  }
  writer->EndObject();
}

bool Timer::is_local(std::string host)
{
  return (std::find(replicas.begin(), replicas.end(), host) != replicas.end());
}

bool Timer::is_last_replica()
{
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);
  return ((!replicas.empty()) ? replicas.back() == localhost : true);
}

bool Timer::is_tombstone()
{
  return ((callback_url == "") && (callback_body == ""));
}

void Timer::become_tombstone()
{
  callback_url = "";
  callback_body = "";

  // Since we're not bringing the start-time forward we have to extend the
  // repeat-for to ensure the tombstone gets added to the replica's store.
  repeat_for = interval_ms * (sequence_number + 1);
}

bool Timer::is_matching_cluster_view_id(std::string cluster_view_id_to_match)
{
  return (cluster_view_id_to_match == cluster_view_id);
}

static void calculate_rendezvous_hash(std::vector<std::string> cluster,
                                      std::vector<uint32_t> cluster_rendezvous_hashes,
                                      TimerID id,
                                      uint32_t replication_factor,
                                      std::vector<std::string>& replicas,
                                      Hasher* hasher)
{
  if (replication_factor == 0u)
  {
    return;
  }

  std::map<uint32_t, size_t> hash_to_idx;
  std::vector<std::string> ordered_cluster;

  // Do a rendezvous hash, by hashing this timer repeatedly, seeded by a
  // different per-server value each time. Rank the servers for this timer
  // based on this hash output.

  for (unsigned int ii = 0;
       ii < cluster.size();
       ++ii)
  {
    uint32_t server_hash = cluster_rendezvous_hashes[ii];
    uint32_t hash = hasher->do_hash(id, server_hash);

    // Deal with hash collisions by incrementing the hash. For
    // example, if I have server hashes A, B, C, D which cause
    // this timer to hash to 10, 40, 10, 30:
    // hash_to_idx[10] = 0 (A's index)
    // hash_to_idx[40] = 1 (B's index)
    // hash_to_idx[10] exists, increment C's hash
    // hash_to_idx[11] = 2 (C's index)
    // hash_to_idx[30] = 3 (D's index)
    //
    // Iterating over hash_to_idx then gives (10, 0), (11, 2), (40, 1)
    // and (30, 3), so the ordered list is A, C, B, D. Effectively, the
    // first entry in the original list consistently wins.
    //
    // This doesn't work perfectly in the edge case
    // If I have servers A, B, C, D which cause this
    // timer to hash to 10, 11, 10, 11:
    // hash_to_idx[10] = 0 (A's index)
    // hash_to_idx[11] = 1 (B's index)
    // hash_to_idx[10] exists, increment C's hash
    // hash_to_idx[11] exists, increment C's hash
    // hash_to_idx[12] = 2 (C's index)
    // hash_to_idx[11] exists, increment D's hash
    // hash_to_idx[12] exists, increment D's hash
    // hash_to_idx[13] = 3 (D's index)
    //
    // Iterating over hash_to_idx then gives (10, 0), (11, 1), (12, 2)
    // and (13, 3), so the ordered list is A, B, C, D. This is wrong,
    // but deterministic - the only problem in this very rare case is that
    // more timers will be moved around when scaling.
    while (hash_to_idx.find(hash) != hash_to_idx.end())
    {
      // LCOV_EXCL_START
      hash++;
      // LCOV_EXCL_STOP
    }

    hash_to_idx[hash] = ii;
  }

  // Pick the lowest hash value as the primary replica.
  for (std::map<uint32_t, size_t>::iterator ii = hash_to_idx.begin();
       ii != hash_to_idx.end();
       ii++)
  {
    ordered_cluster.push_back(cluster[ii->second]);
  }

  replicas.push_back(ordered_cluster.front());

  // Pick the (N-1) highest hash values as the backup replicas.
  replication_factor = replication_factor > ordered_cluster.size() ?
                       ordered_cluster.size() : replication_factor;

  for (size_t jj = 1;
       jj < replication_factor;
       jj++)
  {
    replicas.push_back(ordered_cluster.back());
    ordered_cluster.pop_back();
  }
}

void Timer::calculate_replicas(TimerID id,
                               uint64_t replica_bloom_filter,
                               std::map<std::string, uint64_t> cluster_bloom_filters,
                               std::vector<std::string> cluster,
                               std::vector<uint32_t> cluster_rendezvous_hashes,
                               uint32_t replication_factor,
                               std::vector<std::string>& replicas,
                               std::vector<std::string>& extra_replicas,
                               Hasher* hasher)
{
  std::vector<std::string> bloom_replicas;
  if (replica_bloom_filter)
  {
    // Compare the hash to all the known replicas looking for matches.

    for (std::map<std::string, uint64_t>::iterator it = cluster_bloom_filters.begin();
         it != cluster_bloom_filters.end();
         ++it)
    {
      // Quickly check if this replica might be one of the replicas for the
      // given timer (i.e. if the replica's individual hash collides with the
      // bloom filter we calculated when we created the hash (see `url()`).
      if ((replica_bloom_filter & it->second) == it->second)
      {
        // This is probably a replica.
        bloom_replicas.push_back(it->first);
      }
    }

    // Recreate the vector of replicas. Use the replication factor if it's set,
    // otherwise use the size of the existing replicas.
    replication_factor = replication_factor > 0 ?
                         replication_factor : bloom_replicas.size();
  }

  // Pick replication-factor replicas from the cluster.
  calculate_rendezvous_hash(cluster, cluster_rendezvous_hashes, id, replication_factor, replicas, hasher);

  if (replica_bloom_filter)
  {
    // Finally, add any replicas that were in the bloom filter but aren't in
    // replicas to the extra_replicas vector.
    for (unsigned int ii = 0;
         ii < bloom_replicas.size();
         ++ii)
    {
      if (std::find(replicas.begin(), replicas.end(), bloom_replicas[ii]) == replicas.end())
      {
        extra_replicas.push_back(bloom_replicas[ii]);
      }
    }
  }

  TRC_DEBUG("Replicas calculated:");
  for (std::vector<std::string>::iterator it = replicas.begin();
                                          it != replicas.end();
                                          ++it)
  {
    TRC_DEBUG(" - %s", it->c_str());
  }
}

void Timer::calculate_replicas(uint64_t replica_hash)
{
  std::map<std::string, uint64_t> cluster_bloom_filters;
  __globals->get_cluster_bloom_filters(cluster_bloom_filters);

  std::vector<std::string> cluster;
  __globals->get_cluster_addresses(cluster);

  std::vector<uint32_t> cluster_rendezvous_hashes;
  __globals->get_cluster_hashes(cluster_rendezvous_hashes);

  Timer::calculate_replicas(id,
                            replica_hash,
                            cluster_bloom_filters,
                            cluster,
                            cluster_rendezvous_hashes,
                            _replication_factor,
                            replicas,
                            extra_replicas,
                            &hasher);
}

uint32_t Timer::deployment_id = 0;
uint32_t Timer::instance_id = 0;

// Generate a timer that should be unique across the (possibly geo-redundant) cluster.
// The idea is to use a combination of deployment id, instance id, timestamp and
// an incrementing sequence number.
//
// The ID returned to the client will also contain a
// list of replicas, but this doesn't add much uniqueness.
TimerID Timer::generate_timer_id()
{
  return (TimerID)Utils::generate_unique_integer(Timer::deployment_id,
                                                 Timer::instance_id);
}

// Created tombstones from delete operations are given
// default expires of 10 seconds, if they're found to be
// deleting an existing tombstone, they'll use that timer's
// interval as an expiry.
Timer* Timer::create_tombstone(TimerID id, uint64_t replica_hash)
{
  // Create a tombstone record that will last for 10 seconds.
  Timer* tombstone = new Timer(id, 10000, 10000);
  tombstone->calculate_replicas(replica_hash);
  return tombstone;
}

// Create a Timer object from the JSON representation.
//
// @param id - The unique identity for the timer (see generate_timer_id() above).
// @param replica_hash - The replica hash extracted from the timer URL (or 0 for new timer).
// @param json - The JSON representation of the timer.
// @param error - This will be populated with a descriptive error string if required.
// @param replicated - This will be set to true if this is a replica of a timer.
Timer* Timer::from_json(TimerID id,
                        uint64_t replica_hash,
                        std::string json,
                        std::string& error,
                        bool& replicated)
{
  rapidjson::Document doc;
  doc.Parse<0>(json.c_str());

  if (doc.HasParseError())
  {
    error = "Failed to parse timer as JSON. Error: %s",
            rapidjson::GetParseError_En(doc.GetParseError());
    return NULL;
  }

  return from_json_obj(id, replica_hash, error, replicated, doc);
}

Timer* Timer::from_json_obj(TimerID id,
                            uint64_t replica_hash,
                            std::string& error,
                            bool& replicated,
                            rapidjson::Value& doc)
{
  Timer* timer = NULL;

  try
  {
    JSON_ASSERT_CONTAINS(doc, "timing");
    JSON_ASSERT_CONTAINS(doc, "callback");

    // Parse out the timing block
    rapidjson::Value& timing = doc["timing"];
    JSON_ASSERT_OBJECT(timing);

    JSON_ASSERT_CONTAINS(timing, "interval");
    rapidjson::Value& interval_ms = timing["interval"];
    JSON_ASSERT_INT(interval_ms);

    // Extract the repeat-for parameter, if it's absent, set it to the interval
    // instead.
    int repeat_for_int;
    if (timing.HasMember("repeat-for"))
    {
      JSON_GET_INT_MEMBER(timing, "repeat-for", repeat_for_int);
    }
    else
    {
      repeat_for_int = interval_ms.GetInt();
    }

    if ((interval_ms.GetInt() == 0) && (repeat_for_int != 0))
    {
      // If the interval time is 0 and the repeat_for_int isn't then reject the timer.
      error = "Can't have a zero interval time with a non-zero (%s) repeat-for time",
              std::to_string(repeat_for_int);
      return NULL;
    }

    timer = new Timer(id, (interval_ms.GetInt() * 1000), (repeat_for_int * 1000));

    if (timing.HasMember("start-time-delta"))
    {
      // Timer JSON specified a time offset, use that to determine the true
      // start time.
      uint64_t start_time_delta;
      JSON_GET_INT_64_MEMBER(timing, "start-time-delta", start_time_delta);

      timer->start_time_mono_ms = clock_gettime_ms(CLOCK_MONOTONIC) + start_time_delta;
    }
    else if (timing.HasMember("start-time"))
    {
      // Timer JSON specifies a start-time, use that instead of now.
      uint64_t real_start_time;
      JSON_GET_INT_64_MEMBER(timing, "start-time", real_start_time);
      uint64_t real_time = clock_gettime_ms(CLOCK_REALTIME);
      uint64_t mono_time = clock_gettime_ms(CLOCK_MONOTONIC);

      timer->start_time_mono_ms = mono_time + real_start_time - real_time;
    }

    if (timing.HasMember("sequence-number"))
    {
      JSON_GET_INT_MEMBER(timing, "sequence-number", timer->sequence_number);
    }

    // Parse out the 'callback' block
    rapidjson::Value& callback = doc["callback"];
    JSON_ASSERT_OBJECT(callback);

    JSON_ASSERT_CONTAINS(callback, "http");
    rapidjson::Value& http = callback["http"];
    JSON_ASSERT_OBJECT(http);

    JSON_GET_STRING_MEMBER(http, "uri", timer->callback_url);
    JSON_GET_STRING_MEMBER(http, "opaque", timer->callback_body);

    if (doc.HasMember("reliability"))
    {
      // Parse out the 'reliability' block
      rapidjson::Value& reliability = doc["reliability"];
      JSON_ASSERT_OBJECT(reliability);

      if (reliability.HasMember("cluster-view-id"))
      {
        JSON_GET_STRING_MEMBER(reliability,
                               "cluster-view-id",
                               timer->cluster_view_id);
      }

      if (reliability.HasMember("replicas"))
      {
        rapidjson::Value& replicas = reliability["replicas"];
        JSON_ASSERT_ARRAY(replicas);

        if (replicas.Size() == 0)
        {
          error = "If replicas is specified it must be non-empty";
          delete timer; timer = NULL;
          return NULL;
        }

        timer->_replication_factor = replicas.Size();

        for (rapidjson::Value::ConstValueIterator it = replicas.Begin();
                                                  it != replicas.End();
                                                  ++it)
        {
          JSON_ASSERT_STRING(*it);
          timer->replicas.push_back(std::string(it->GetString(), it->GetStringLength()));
        }
      }
      else
      {
        if (reliability.HasMember("replication-factor"))
        {
          JSON_GET_INT_MEMBER(reliability,
                              "replication-factor",
                              timer->_replication_factor);
        }
        else
        {
          // Default replication factor is 2.
          timer->_replication_factor = 2;
        }
      }
    }
    else
    {
      // Default to 2 replicas
      timer->_replication_factor = 2;
    }

    timer->_replica_tracker = pow(2, timer->_replication_factor) - 1;

    if (timer->replicas.empty())
    {
      // Replicas not determined above, determine them now.  Note that this implies
      // the request is from a client, not another replica.
      replicated = false;
      timer->calculate_replicas(replica_hash);
    }
    else
    {
      // Replicas were specified in the request, must be a replication message
      // from another cluster node.
      replicated = true;
    }

    if (doc.HasMember("statistics"))
    {
      // Parse out the 'statistics' block
      rapidjson::Value& statistics = doc["reliability"];
      JSON_ASSERT_OBJECT(statistics);

      if (statistics.HasMember("tags"))
      {
        rapidjson::Value& tags = statistics["tags"];
        JSON_ASSERT_ARRAY(tags);

        for (rapidjson::Value::ConstValueIterator it = tags.Begin();
                                                  it != tags.End();
                                                  ++it)
        {
          // All tags get store within the timer as a vector
          JSON_ASSERT_STRING(*it);
          timer->tags.push_back(std::string(it->GetString(), it->GetStringLength()));
        }
      }
    }
  }
  catch (JsonFormatError err)
  {
    error = "Badly formed Timer entry - hit error on line " + std::to_string(err._line);
    delete timer; timer = NULL;
    return NULL;
  }

  return timer;
}

int Timer::update_replica_tracker(int replica_index)
{
  _replica_tracker = _replica_tracker % (int)pow(2,replica_index);
  return _replica_tracker;
}

bool Timer::has_replica_been_informed(int replica_index)
{
  return ((_replica_tracker & (int)pow(2, replica_index)) == 0);
}

void Timer::update_cluster_information()
{
  // Update the replica list
  replicas.clear();
  calculate_replicas(0);

  // Update the cluster view ID
  std::string global_cluster_view_id;
  __globals->get_cluster_view_id(global_cluster_view_id);
 cluster_view_id = global_cluster_view_id;
}
