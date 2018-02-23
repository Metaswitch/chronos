/**
 * @file chronos_internal_connection.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <string>
#include <map>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"

#include "utils.h"
#include "log.h"
#include "sas.h"
#include "sasevent.h"
#include "httpconnection.h"
#include "chronos_internal_connection.h"
#include "json_parse_utils.h"
#include "timer.h"
#include "constants.h"
#include "globals.h"
#include "chronos_pd_definitions.h"

ChronosInternalConnection::ChronosInternalConnection(HttpResolver* resolver,
                                                     TimerHandler* handler,
                                                     Replicator* replicator,
                                                     Alarm* alarm,
                                                     SNMP::U32Scalar* remaining_nodes_scalar,
                                                     SNMP::CounterTable* timers_processed_table,
                                                     SNMP::CounterTable* invalid_timers_processed_table,
                                                     bool resync_on_start) :
  _handler(handler),
  _replicator(replicator),
  _alarm(alarm),
  _remaining_nodes_scalar(remaining_nodes_scalar),
  _timers_processed_table(timers_processed_table),
  _invalid_timers_processed_table(invalid_timers_processed_table)
{
  std::string bind_address;
  __globals->get_bind_address(bind_address);

  _http = new HttpClient(false,
                         resolver,
                         nullptr,
                         nullptr,
                         SASEvent::HttpLogLevel::NONE,
                         nullptr,
                         false,
                         false,
                         -1,
                         false,
                         "",
                         bind_address);

  // Create an updater to control when Chronos should resynchronise. This uses
  // SIGUSR1 rather than the default SIGHUP, and we should resynchronise on
  // start up
  _updater = new Updater<void, ChronosInternalConnection>
                   (this,
                   std::mem_fun(&ChronosInternalConnection::resynchronize),
                   &_sigusr1_handler,
                   resync_on_start);

  // Zero the statistic to start with
  if (_remaining_nodes_scalar)
  {
    _remaining_nodes_scalar->value = 0;
  }
}

ChronosInternalConnection::~ChronosInternalConnection()
{
  delete _updater; _updater = NULL;
  delete _http; _http = NULL;
}

void ChronosInternalConnection::resynchronize()
{
  // Get the cluster nodes
  std::vector<std::string> cluster_nodes;
  std::vector<std::string> joining_nodes;
  __globals->get_cluster_joining_addresses(joining_nodes);
  std::vector<std::string> staying_nodes;
  __globals->get_cluster_staying_addresses(staying_nodes);
  std::vector<std::string> leaving_nodes;
  __globals->get_cluster_leaving_addresses(leaving_nodes);

  cluster_nodes.insert(cluster_nodes.end(),
                       joining_nodes.begin(),
                       joining_nodes.end());
  cluster_nodes.insert(cluster_nodes.end(),
                       staying_nodes.begin(),
                       staying_nodes.end());
  cluster_nodes.insert(cluster_nodes.end(),
                       leaving_nodes.begin(),
                       leaving_nodes.end());

  // Shuffle the lists (so the same Chronos node doesn't get queried by
  // all the other nodes at the same time) and remove the local node
  std::random_shuffle(cluster_nodes.begin(), cluster_nodes.end());

  // Start the resync operation. Update the logs/stats/alarms
  if (_alarm)
  {
    _alarm->set();  // LCOV_EXCL_LINE - No alarms in UT
  }

  CL_CHRONOS_START_RESYNC.log();
  TRC_DEBUG("Starting resynchronization operation");

  int nodes_remaining = cluster_nodes.size();
  int default_port;
  __globals->get_bind_port(default_port);
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);

  for (std::vector<std::string>::iterator it = cluster_nodes.begin();
                                          it != cluster_nodes.end();
                                          ++it, --nodes_remaining)
  {
    // Update the number of nodes to query
    if (_remaining_nodes_scalar != NULL)
    {
      _remaining_nodes_scalar->value = nodes_remaining;
    }

    std::string server_to_sync = Utils::uri_address(*it, default_port);

    HTTPCode rc = resynchronise_with_single_node(server_to_sync,
                                                 cluster_nodes,
                                                 localhost);
    if (rc != HTTP_OK)
    {
      TRC_WARNING("Resynchronisation with node %s failed with rc %d",
                  server_to_sync.c_str(),
                  rc);
      CL_CHRONOS_RESYNC_ERROR.log(server_to_sync.c_str());
    }
  }

  // The resync operation is now complete. Update the logs/stats/alarms
  TRC_DEBUG("Finished resynchronization operation");

  CL_CHRONOS_COMPLETE_RESYNC.log();

  if (_alarm)
  {
    _alarm->clear();   // LCOV_EXCL_LINE - No alarms in UT
  }

  if (_remaining_nodes_scalar != NULL)
  {
    _remaining_nodes_scalar->value = 0;
  }
}

HTTPCode ChronosInternalConnection::resynchronise_with_single_node(
                             const std::string& server_to_sync,
                             std::vector<std::string> cluster_nodes,
                             std::string localhost)
{
  TRC_DEBUG("Querying %s for timers", server_to_sync.c_str());

  // Get the cluster view ID from the global configuration
  std::string cluster_view_id;
  __globals->get_cluster_view_id(cluster_view_id);

  uint32_t current_time = Utils::get_time();
  uint32_t time_from = 0;
  bool use_time_from_param = false;
  std::string response;
  HTTPCode rc;

  // Loop sending GETs to the server while the response is a 206
  do
  {
    std::map<TimerID, int> delete_map;
    std::string path = create_path(localhost,
                                   cluster_view_id,
                                   time_from,
                                   use_time_from_param);
    rc = send_get(server_to_sync,
                  path,
                  MAX_TIMERS_IN_RESPONSE,
                  response);

    use_time_from_param = true;

    if ((rc == HTTP_PARTIAL_CONTENT) ||
        (rc == HTTP_OK))
    {
      // Parse the GET response
      rapidjson::Document doc;
      doc.Parse<0>(response.c_str());

      if (doc.HasParseError())
      {
        // We've failed to parse the document as JSON. This suggests that
        // there's something seriously wrong with the node we're trying
        // to query so don't retry
        TRC_WARNING("Failed to parse document as JSON");
        rc = HTTP_BAD_REQUEST;
        break;
      }

      try
      {
        JSON_ASSERT_CONTAINS(doc, JSON_TIMERS);
        JSON_ASSERT_ARRAY(doc[JSON_TIMERS]);
        const rapidjson::Value& ids_arr = doc[JSON_TIMERS];
        int total_timers = ids_arr.Size();
        int count_invalid_timers = 0;

        for (rapidjson::Value::ConstValueIterator ids_it = ids_arr.Begin();
             ids_it != ids_arr.End();
             ++ids_it)
        {
          try
          {
            const rapidjson::Value& id_arr = *ids_it;
            JSON_ASSERT_OBJECT(id_arr);

            // Get the timer ID
            TimerID timer_id;
            JSON_GET_INT_64_MEMBER(id_arr, JSON_TIMER_ID, timer_id);

            // Get the old replicas
            std::vector<std::string> old_replicas;
            JSON_ASSERT_CONTAINS(id_arr, JSON_OLD_REPLICAS);
            JSON_ASSERT_ARRAY(id_arr[JSON_OLD_REPLICAS]);
            const rapidjson::Value& old_repl_arr = id_arr[JSON_OLD_REPLICAS];
            for (rapidjson::Value::ConstValueIterator repl_it = old_repl_arr.Begin();
                                                      repl_it != old_repl_arr.End();
                                                      ++repl_it)
            {
              JSON_ASSERT_STRING(*repl_it);
              old_replicas.push_back(repl_it->GetString());
            }

            // Get the timer.
            JSON_ASSERT_CONTAINS(id_arr, JSON_TIMER);
            JSON_ASSERT_OBJECT(id_arr[JSON_TIMER]);
            const rapidjson::Value& timer_obj = id_arr[JSON_TIMER];

            bool store_timer = false;
            std::string error_str;
            bool replicated_timer;
            bool unused_gr_replicated_timer;

            Timer* timer = Timer::from_json_obj(timer_id,
                                                0,
                                                0,
                                                error_str,
                                                replicated_timer,
                                                unused_gr_replicated_timer,
                                                (rapidjson::Value&)timer_obj);

            if (!timer)
            {
              count_invalid_timers++;
              TRC_INFO("Unable to create timer - error: %s", error_str.c_str());
              continue;
            }
            else if (!replicated_timer)
            {
              count_invalid_timers++;
              TRC_INFO("Unreplicated timer in response - ignoring");
              delete timer; timer = NULL;
              continue;
            }

            // Update our view of the newest timer we've processed
            time_from = timer->next_pop_time() - current_time + 1;

            // Decide what we're going to do with this timer.
            int old_level = 0;
            bool in_old_replica_list = get_replica_level(old_level,
                                                         localhost,
                                                         old_replicas);
            int new_level = 0;
            bool in_new_replica_list = get_replica_level(new_level,
                                                         localhost,
                                                         timer->replicas);

            // Add the timer to the delete map we're building up
            delete_map.insert(std::pair<TimerID, int>(timer_id, new_level));

            if (in_new_replica_list)
            {
              // Add the timer to my store if I can.
              if (in_old_replica_list)
              {
                if (old_level >= new_level)
                {
                  // Add/update timer
                  // LCOV_EXCL_START - Adding timer paths are tested elsewhere
                  store_timer = true;
                  // LCOV_EXCL_STOP
                }
              }
              else
              {
                // Add/update timer
                store_timer = true;
              }

              // Now loop through the new replicas.
              int index = 0;
              for (std::vector<std::string>::iterator it = timer->replicas.begin();
                                                      it != timer->replicas.end();
                                                      ++it, ++index)
              {
                if (index <= new_level)
                {
                  // Do nothing. We've covered adding the timer to the store above
                }
                else
                {
                  // We can potentially replicate the timer to one of these nodes.
                  // Check whether the new replica was involved previously
                  int old_rep_level = 0;
                  bool is_new_rep_in_old_rep = get_replica_level(old_rep_level,
                                                                 *it,
                                                                 old_replicas);
                  if (is_new_rep_in_old_rep)
                  {
                    if (old_rep_level >= new_level)
                    {
                      _replicator->replicate_timer_to_node(timer, *it);
                    }
                  }
                  else
                  {
                    _replicator->replicate_timer_to_node(timer, *it);
                  }
                }
              }

              // Now loop through the old replicas. We can send a tombstone
              // replication to any node that used to be a replica and was
              // higher in the replica list than the new replica.
              index = 0;
              for (std::vector<std::string>::iterator it = old_replicas.begin();
                                                      it != old_replicas.end();
                                                      ++it, ++index)
              {
                if (index >= new_level)
                {
                  // We can potentially tombstone the timer to one of these nodes.
                  bool old_rep_in_new_rep = get_replica_presence(*it,
                                                                 timer->replicas);

                  if (!old_rep_in_new_rep)
                  {
                    Timer* timer_copy = new Timer(*timer);
                    timer_copy->become_tombstone();
                    _replicator->replicate_timer_to_node(timer_copy, *it);
                    delete timer_copy; timer_copy = NULL;
                  }
                }
              }
            }

            // Add the timer to the store if we can. This is done
            // last so we don't invalidate the pointer to the timer.
            if (store_timer)
            {
              _handler->add_timer(timer);
              timer = NULL;
            }
            else
            {
              delete timer; timer = NULL;
            }

            // Finally, note that we processed the timer
            if (_timers_processed_table != NULL)
            {
              _timers_processed_table->increment();
            }

          }
          catch (JsonFormatError& err)
          {
            // A single entry is badly formatted. This is unexpected but we'll try
            // to keep going and process the rest of the timers.
            count_invalid_timers++;
            if (_invalid_timers_processed_table != NULL)
            {
              _invalid_timers_processed_table->increment();
            }
            TRC_INFO("JSON entry was invalid (hit error at %s:%d)",
                     err._file, err._line);
          }
        }

        // Check if we were able to successfully process any timers - if not
        // then bail out as there's something wrong with the node we're
        // querying
        if ((total_timers != 0) &&
           (count_invalid_timers == total_timers))
        {
          TRC_WARNING("Unable to process any timer entries in GET response");
          rc = HTTP_BAD_REQUEST;
        }
      }
      catch (JsonFormatError& err)
      {
        // We've failed to find the Timers array. This suggests that
        // there's something seriously wrong with the node we're trying
        // to query so don't retry
        TRC_WARNING("JSON body didn't contain the Timers array");
        rc = HTTP_BAD_REQUEST;
      }

      // Send a DELETE to all the nodes to update their timer references
      if (!delete_map.empty())
      {
        std::string delete_body = create_delete_body(delete_map);
        int default_port;
        __globals->get_bind_port(default_port);

        for (std::vector<std::string>::iterator it = cluster_nodes.begin();
                                                it != cluster_nodes.end();
                                                ++it)
        {
          std::string delete_server = Utils::uri_address(*it, default_port);
          HTTPCode delete_rc = send_delete(delete_server, delete_body);
          if (delete_rc != HTTP_ACCEPTED)
          {
            // We've received an error response to the DELETE request. There's
            // not much more we can do here (a timeout will have already
            // been retried). A failed DELETE won't prevent the resync operation
            // from finishing, it just means that we'll tell other nodes
            // about timers inefficiently.
            TRC_INFO("Error response (%d) to DELETE request to %s",
                     delete_rc,
                    (*it).c_str());
          }
        }
      }
    }
    else
    {
      // We've received an error response to the GET request. A timeout
      // will already have been retried by the underlying HTTPConnection,
      // so don't retry again
      TRC_WARNING("Error response (%d) to GET request to %s",
                  rc,
                  server_to_sync.c_str());
    }
  }
  while (rc == HTTP_PARTIAL_CONTENT);

  return rc;
}

HTTPCode ChronosInternalConnection::send_delete(const std::string& server,
                                                const std::string& body)
{
  std::string path = "/timers/references";
  HttpRequest req = build_request(server, path, HttpClient::RequestType::DELETE);
  req.set_body(body);
  HttpResponse resp = req.send();
  HTTPCode rc = resp.get_rc();

  return rc;
}

std::string ChronosInternalConnection::create_path(
                                     const std::string& node_for_replicas_param,
                                     std::string cluster_view_id_param,
                                     uint32_t time_from_param,
                                     bool use_time_from_param)
{
  std::string path = std::string("/timers?") +
                     PARAM_NODE_FOR_REPLICAS + "="  + node_for_replicas_param + ";" +
                     PARAM_CLUSTER_VIEW_ID + "="  + cluster_view_id_param;

  if (use_time_from_param)
  {
    path += std::string(";") +
            PARAM_TIME_FROM + "=" + std::to_string(time_from_param);
  }

  return path;
}

HTTPCode ChronosInternalConnection::send_get(const std::string& server,
                                             const std::string& path,
                                             int max_timers,
                                             std::string& response)
{
  std::string range_header = std::string(HEADER_RANGE) + ":" +
                             std::to_string(MAX_TIMERS_IN_RESPONSE);

  HttpRequest req = build_request(server, path, HttpClient::RequestType::GET);
  req.add_header(range_header);
  HttpResponse  resp = req.send();
  HTTPCode rc = resp.get_rc();
  response = resp.get_body();

  return rc;
}

std::string ChronosInternalConnection::create_delete_body(std::map<TimerID, int> delete_map)
{
  // Create the JSON doc
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();

  writer.String(JSON_IDS);
  writer.StartArray();

  for (std::map<TimerID, int>::iterator it = delete_map.begin();
                                        it != delete_map.end();
                                        ++it)
  {
    writer.StartObject();
    {
      writer.String(JSON_ID);
      writer.Int64(it->first);
      writer.String(JSON_REPLICA_INDEX);
      writer.Int(it->second);
    }
    writer.EndObject();
  }

  writer.EndArray();
  writer.EndObject();

  return sb.GetString();
}

bool ChronosInternalConnection::get_replica_presence(std::string current_node,
                                                     std::vector<std::string> replicas)
{
  int unused_index;
  return get_replica_level(unused_index, current_node, replicas);
}

bool ChronosInternalConnection::get_replica_level(int& index,
                                                  std::string current_node,
                                                  std::vector<std::string> replicas)
{
  for (std::vector<std::string>::iterator it = replicas.begin();
                                          it != replicas.end();
                                          ++it, ++index)
  {
    if (*it == current_node)
    {
      return true;
    }
  }

  return false;
}

// LCOV_EXCL_START - In UTs, we test a subclass that overrides this method, to
// allow us to return a MockHttpRequest
HttpRequest ChronosInternalConnection::build_request(
                                                 const std::string& server,
                                                 const std::string& path,
                                                 HttpClient::RequestType method)
{
  HttpRequest req(server,
                                  "http",
                                  _http,
                                  method,
                                  path);
  return req;
}
// LCOV_EXCL_STOP
