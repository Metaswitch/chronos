/**
 * @file timer.cpp
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

uint32_t DELAY_BETWEEN_CHRONOS_INSTANCES_MS = 2000;

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
  sites(std::vector<std::string>()),
  tags(std::map<std::string, uint32_t>()),
  callback_url(""),
  callback_body(""),
  _replication_factor(0)
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

uint32_t Timer::delay_from_replica_position() const
{
  // Get the replica position
  std::string localhost;
  int replica_index = 0;
  __globals->get_cluster_local_ip(localhost);

  replica_index = std::find(replicas.begin(), replicas.end(), localhost) -
                                                                  replicas.begin();

  // Delay by 2 seconds for each place down in the replica list
  return replica_index * DELAY_BETWEEN_CHRONOS_INSTANCES_MS;
}

uint32_t Timer::delay_from_site_position() const
{
  // Get the site position
  std::string local_site_name;
  int site_index = 0;
  __globals->get_local_site_name(local_site_name);

  site_index = std::find(sites.begin(), sites.end(), local_site_name) -
                                                                  sites.begin();

  // Delay for each site ahead of us in the site list. The delay for each site
  // is 2 seconds * number of replicas
  return site_index * _replication_factor * DELAY_BETWEEN_CHRONOS_INSTANCES_MS;
}

uint32_t Timer::delay_from_sequence_position() const
{
  // Delay depending where this timer is in its sequence. This affects
  // repeating timers (e.g. a timer that's due to pop every 20 secs for the
  // next 100 secs).
  return (sequence_number + 1) * interval_ms;
}

// Returns the next pop time in ms.
uint32_t Timer::next_pop_time() const
{
  return start_time_mono_ms +
         delay_from_sequence_position() +
         delay_from_replica_position() +
         delay_from_site_position();
}

uint64_t Timer::get_pop_time() const
{
  // The timer heap operates on 64-bit numbers, and expects times to overflow
  // at the 64-bit overflow point, whereas Chronos uses 32-bit numbers. If we
  // just provide 32-bit numbers to the heap, they will wrap at the wrong point
  // and our overflow tests will fail. To avoid that, we shift the pop time 32
  // bits to the left when providing it to the heap, so that times are still in
  // the same order but they wrap at the 64-bit overflow point.
  //
  // This time is only used for heap ordering - when we get this out of the
  // heap, we'll use next_pop_time() which returns the right time.
  return (uint64_t)next_pop_time() << 32;
}

// Create the timer's URL from a given hostname
std::string Timer::url(std::string host)
{
  std::stringstream ss;

  if (host != "")
  {
    int default_port;
    __globals->get_bind_port(default_port);

    ss << "http://" << Utils::uri_address(host, default_port);
  }

  ss << "/timers/";
  // We render the timer ID as a 0-padded hex string so we can parse it back out
  // later easily.
  ss << std::setfill('0') << std::setw(16) << std::hex << id;

  Globals::TimerIDFormat timer_id_format;
  __globals->get_timer_id_format(timer_id_format);

  if (timer_id_format == Globals::TimerIDFormat::WITHOUT_REPLICAS)
  {
    ss << "-" << std::to_string(_replication_factor);
  }
  else
  {
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
  }

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
//         "sites": ["site name 1", "site name 2",...],
//         "replicas": ["replica 1", "replica 2",...]
//     }
//     "statistics": {
//         "tag-info": [ {"type": <TAG>,
//                        "count": <uint>},
//                       {"type": <TAG2>,
//                        "count": <uint2>}
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

      if (!replicas.empty())
      {
        writer->String("replicas");
        writer->StartArray();
        {
          for (std::string replica : replicas)
          {
            writer->String(replica.c_str());
          }
        }

        writer->EndArray();
      }

      if (!sites.empty())
      {
        writer->String("sites");
        writer->StartArray();
        {
          for (std::string site: sites)
          {
            writer->String(site.c_str());
          }
        }
        writer->EndArray();
      }
    }
    writer->EndObject();

    writer->String("statistics");
    writer->StartObject();
    {
      writer->String("tag-info");
      writer->StartArray();
      {
        for (std::map<std::string, uint32_t>::iterator it = tags.begin();
                                                       it != tags.end();
                                                       ++it)
        {
          writer->StartObject();
          {
            writer->String("type");
            writer->String(it->first.c_str());
            writer->String("count");
            writer->Int(it->second);
          }
          writer->EndObject();
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
       ++ii)
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
                               std::vector<std::string> new_cluster,
                               std::vector<uint32_t> new_cluster_rendezvous_hashes,
                               std::vector<std::string> old_cluster,
                               std::vector<uint32_t> old_cluster_rendezvous_hashes,
                               uint32_t replication_factor,
                               std::vector<std::string>& replicas,
                               std::vector<std::string>& extra_replicas,
                               Hasher* hasher)
{
  std::vector<std::string> old_replicas;
  replicas.clear();

  // Calculate the replicas for the current cluster.
  calculate_rendezvous_hash(new_cluster,
                            new_cluster_rendezvous_hashes,
                            id,
                            replication_factor,
                            replicas,
                            hasher);

  // Calculate what the replicas would have been in the previous cluster.
  calculate_rendezvous_hash(old_cluster,
                            old_cluster_rendezvous_hashes,
                            id,
                            replication_factor,
                            old_replicas,
                            hasher);

  // Set any nodes that were replicas in the old cluster but aren't in the
  // current cluster in extra_replicas to ensure that these replicas get
  // deleted.
  for (unsigned int ii = 0; ii < old_replicas.size(); ++ii)
  {
    if (std::find(replicas.begin(), replicas.end(), old_replicas[ii]) == replicas.end())
    {
      extra_replicas.push_back(old_replicas[ii]);
    }
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
  calculate_rendezvous_hash(cluster,
                            cluster_rendezvous_hashes,
                            id,
                            replication_factor,
                            replicas,
                            hasher);

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
  std::vector<std::string> new_cluster;
  std::vector<std::string> joining_cluster_addresses;
  __globals->get_cluster_staying_addresses(new_cluster);
  __globals->get_cluster_joining_addresses(joining_cluster_addresses);
  new_cluster.insert(new_cluster.end(),
                     joining_cluster_addresses.begin(),
                     joining_cluster_addresses.end());

  std::vector<std::string> old_cluster;
  std::vector<std::string> leaving_cluster_addresses;
  __globals->get_cluster_staying_addresses(old_cluster);
  __globals->get_cluster_leaving_addresses(leaving_cluster_addresses);
  old_cluster.insert(old_cluster.end(),
                     leaving_cluster_addresses.begin(),
                     leaving_cluster_addresses.end());

  std::vector<uint32_t> new_cluster_rendezvous_hashes;
  __globals->get_new_cluster_hashes(new_cluster_rendezvous_hashes);

  std::vector<uint32_t> old_cluster_rendezvous_hashes;
  __globals->get_old_cluster_hashes(old_cluster_rendezvous_hashes);

  std::map<std::string, uint64_t> cluster_bloom_filters;
  __globals->get_cluster_bloom_filters(cluster_bloom_filters);

  // How we calculate the replicas depends on the supported Timer ID format
  Globals::TimerIDFormat timer_id_format;
  __globals->get_timer_id_format(timer_id_format);

  if (timer_id_format == Globals::TimerIDFormat::WITHOUT_REPLICAS)
  {
    calculate_replicas(id,
                       new_cluster,
                       new_cluster_rendezvous_hashes,
                       old_cluster,
                       old_cluster_rendezvous_hashes,
                       _replication_factor,
                       replicas,
                       extra_replicas,
                       &hasher);
  }
  else
  {
    calculate_replicas(id,
                       replica_hash,
                       cluster_bloom_filters,
                       new_cluster,
                       new_cluster_rendezvous_hashes,
                       _replication_factor,
                       replicas,
                       extra_replicas,
                       &hasher);
  }
}

void Timer::populate_sites()
{
  std::string local_site_name;
  __globals->get_local_site_name(local_site_name);

  std::vector<std::string> remote_site_names;
  __globals->get_remote_site_names(remote_site_names);

  sites.push_back(local_site_name);

  std::random_shuffle(remote_site_names.begin(), remote_site_names.end());
  for (std::string remote_site_name: remote_site_names)
  {
    sites.push_back(remote_site_name);
  }
}


void Timer::update_sites_on_timer_pop()
{
  std::string local_site_name;
  __globals->get_local_site_name(local_site_name);

  std::vector<std::string> remote_site_names;
  __globals->get_remote_site_names(remote_site_names);

  std::vector<std::string> site_names;

  // Build up a new list of sites
  // - Firstly, remove any sites that no longer exist
  // - Secondly, add any new sites to the end of the list (local site first)
  for (std::string site: sites)
  {
    std::vector<std::string>::iterator pos =
            std::find(remote_site_names.begin(), remote_site_names.end(), site);

    if (pos != remote_site_names.end())
    {
      site_names.push_back(site);
      remote_site_names.erase(pos);
    }
    else if (site == local_site_name)
    {
      site_names.push_back(site);
    }
    else
    {
      TRC_DEBUG("Removing site (%s) as it no longer exists", site.c_str());
    }
  }

  if (std::find(site_names.begin(), site_names.end(), local_site_name)
        == site_names.end())
  {
    site_names.push_back(local_site_name);
  }

  // Shuffle the remote sites
  std::random_shuffle(remote_site_names.begin(), remote_site_names.end());
  for (std::string site: remote_site_names)
  {
    TRC_DEBUG("Adding remote site (%s) to sites", site.c_str());
    site_names.push_back(site);
  }

  sites = site_names;
}

// Generate a timer that should be unique across the (possibly geo-redundant)
// cluster. The idea is to use a combination of deployment id, instance id,
// timestamp and an incrementing sequence number.
TimerID Timer::generate_timer_id()
{
  uint32_t instance_id = 0;
  uint32_t deployment_id = 0;

  __globals->get_instance_id(instance_id);
  __globals->get_deployment_id(deployment_id);

  return (TimerID)Utils::generate_unique_integer(deployment_id,
                                                 instance_id);
}

// Created tombstones from delete operations are given
// default expires of 10 seconds, if they're found to be
// deleting an existing tombstone, they'll use that timer's
// interval as an expiry.
Timer* Timer::create_tombstone(TimerID id,
                               uint64_t replica_hash,
                               uint32_t replication_factor)
{
  // Create a tombstone record that will last for 10 seconds.
  Timer* tombstone = new Timer(id, 10000, 10000);
  tombstone->_replication_factor = replication_factor;
  tombstone->calculate_replicas(replica_hash);
  tombstone->populate_sites();
  return tombstone;
}

// Create a Timer object from the JSON representation.
//
// @param id - The unique identity for the timer (see generate_timer_id()
//             above
// @param replication_factor - The replication_factor extracted from the timer
//                             URL (or 0 for new timer)
// @param json - The JSON representation of the timer
// @param error - This will be populated with a descriptive error string if
//                 required
// @param replicated - This will be set to true if this is a replica of a timer
// @param gr_replicated - This will be set to true if this isn't the first site
//                        to process this timer
Timer* Timer::from_json(TimerID id,
                        uint32_t replication_factor,
                        uint64_t replica_hash,
                        std::string json,
                        std::string& error,
                        bool& replicated,
                        bool& gr_replicated)
{
  rapidjson::Document doc;
  doc.Parse<0>(json.c_str());

  if (doc.HasParseError())
  {
    error = "Failed to parse timer as JSON. Error: ";
    error.append(rapidjson::GetParseError_En(doc.GetParseError()));
    return NULL;
  }

  return from_json_obj(id,
                       replication_factor,
                       replica_hash,
                       error,
                       replicated,
                       gr_replicated,
                       doc);
}

Timer* Timer::from_json_obj(TimerID id,
                            uint32_t replication_factor,
                            uint64_t replica_hash,
                            std::string& error,
                            bool& replicated,
                            bool& gr_replicated,
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
    rapidjson::Value& interval_s = timing["interval"];
    JSON_ASSERT_INT(interval_s);

    // Extract the repeat-for parameter, if it's absent, set it to the interval
    // instead.
    int repeat_for_int;
    if (timing.HasMember("repeat-for"))
    {
      JSON_GET_INT_MEMBER(timing, "repeat-for", repeat_for_int);
    }
    else
    {
      repeat_for_int = interval_s.GetInt();
    }

    if ((interval_s.GetInt() == 0) && (repeat_for_int != 0))
    {
      // If the interval time is 0 and the repeat_for_int isn't then reject the timer.
      error = "Can't have a zero interval time with a non-zero (";
      error.append(std::to_string(repeat_for_int));
      error.append(") repeat-for time");
      return NULL;
    }

    timer = new Timer(id, (interval_s.GetInt() * 1000), (repeat_for_int * 1000));

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

        timer->_replication_factor = (replication_factor > 0) ?
                                      replication_factor :
                                      replicas.Size();

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

          // If the URL contained a replication factor then this replication
          // factor must match the replication factor in the JSON body.
          if ((replication_factor > 0) &&
              (timer->_replication_factor != replication_factor))
          {
            error = "Replication factor on the timer ID (";
            error.append(std::to_string(replication_factor));
            error.append(") doesn't match the JSON body (");
            error.append(std::to_string(timer->_replication_factor));
            error.append(")");
            delete timer; timer = NULL;
            return NULL;
          }
        }
        else
        {
          // If the URL contained a replication factor, use that, otherwise
          // default replication factor is 2.
          timer->_replication_factor = replication_factor ? replication_factor : 2;
        }
      }

      if (reliability.HasMember("sites"))
      {
        rapidjson::Value& sites = reliability["sites"];
        JSON_ASSERT_ARRAY(sites);

        for (rapidjson::Value::ConstValueIterator it = sites.Begin();
                                                  it != sites.End();
                                                  ++it)
        {
          JSON_ASSERT_STRING(*it);
          timer->sites.push_back(std::string(it->GetString(), it->GetStringLength()));
        }
      }
    }
    else
    {
      // If the URL contained a replication factor, use that, otherwise
      // default replication factor is 2.
      timer->_replication_factor = replication_factor ? replication_factor : 2;
    }

    if (timer->replicas.empty())
    {
      // Replicas not determined above, determine them now. Note that this
      // implies the request is from a client (or a Chronos in a different
      // site), not another replica.
      replicated = false;
      timer->calculate_replicas(replica_hash);
    }
    else
    {
      // Replicas were specified in the request, must be a replication message
      // from another cluster node.
      replicated = true;
    }

    if (timer->sites.empty())
    {
      // Sites not determined above, determine them now. Note that this implies
      // the request is from a client, not another replica.
      gr_replicated = false;
      timer->populate_sites();
    }
    else
    {
      gr_replicated = true;
    }

    if ((doc.HasMember("statistics")) &&
        (doc["statistics"].IsObject()))
    {
      // Parse out the 'statistics' block.
      rapidjson::Value& statistics = doc["statistics"];

      if ((statistics.HasMember("tag-info")) &&
          (statistics["tag-info"].IsArray()))
      {
        rapidjson::Value& tag_info = statistics["tag-info"];

        // Iterate over tag-info array, pulling out all valid tags and counts.
        for (rapidjson::Value::ValueIterator it = tag_info.Begin();
                                                  it != tag_info.End();
                                                  ++it)
        {

          // Check that we have an object, and it contains a "type".
          // If not, discard this tag.
          if (((*it).IsObject())       &&
               (it->HasMember("type")))
          {
            rapidjson::Value& type = (*it)["type"];
            // Check that the "type" is a string.
            // If not, discard this tag, and move to next tag-info object.
            if (!(type.IsString()))
            {
              TRC_DEBUG("Tag type badly formed. Discarding some tags.");
              continue;
            }

            // Default tag count if no value is found in the JSON object.
            uint32_t count = 1;

            // If a count is provided, check it is a uint.
            // If not a uint, discard this tag, and move to next tag-info object.
            if (it->HasMember("count"))
            {
              if (!((*it)["count"].IsUint()))
              {
                TRC_DEBUG("Tag \"%s\" has an invalid count value. Discarding some tags.",
                          type.GetString());
                continue;
              }
              count = (*it)["count"].GetUint();
            }

            // Add the tag to the timer.
            timer->tags[(type.GetString())] += count;
          }
          else
          {
            TRC_DEBUG("Tag-info object badly formed, or missing type. Discarding some tags.");
          }
        }
      }
      else
      {
        TRC_DEBUG("Tag-info array not present, or badly formed. Discarding all tags.");
      }
    }
    else
    {
      TRC_DEBUG("Statistics object not present, or badly formed. Discarding all tags.");
    }
  }
  catch (JsonFormatError& err)
  {
    error = "Badly formed Timer entry - hit error on line " + std::to_string(err._line);
    delete timer; timer = NULL;
    return NULL;
  }

  return timer;
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
