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
                                                     LastValueCache* lvc,
                                                     Alarm* alarm) :
  _http(new HttpConnection("",
                           false,
                           resolver,
                           SASEvent::HttpLogLevel::DETAIL,
                           NULL)),
  _handler(handler),
  _replicator(replicator),
  _alarm(alarm),
  _nodes_to_query_stat(new Statistic("chronos_scale_nodes_to_query", lvc)),
  _timers_processed_stat(new StatisticCounter("chronos_scale_timers_processed", lvc)),
  _invalid_timers_processed_stat(new StatisticCounter("chronos_scale_invalid_timers_processed", lvc))
{
  // Create an updater to control when Chronos should resynchronise. This uses 
  // SIGUSR1 rather than the default SIGHUP, and we shouldn't resynchronise
  // on start up (note this may change in future work)
  _updater = new Updater<void, ChronosInternalConnection>
                   (this, 
                   std::mem_fun(&ChronosInternalConnection::resynchronize), 
                   &_sigusr1_handler, 
                   false);
  
  // Zero the statistic to start with
  std::vector<std::string> no_stats;
  no_stats.push_back(std::to_string(0));
  _nodes_to_query_stat->report_change(no_stats);
}

ChronosInternalConnection::~ChronosInternalConnection()
{
  delete _updater; _updater = NULL;
  delete _invalid_timers_processed_stat; _invalid_timers_processed_stat = NULL;
  delete _timers_processed_stat; _timers_processed_stat = NULL;
  delete _nodes_to_query_stat; _nodes_to_query_stat = NULL;
  delete _http; _http = NULL;
}

void ChronosInternalConnection::resynchronize()
{
  // Get the cluster nodes
  std::vector<std::string> cluster_nodes;
  __globals->get_cluster_addresses(cluster_nodes);
  std::vector<std::string> leaving_nodes;
  __globals->get_cluster_leaving_addresses(leaving_nodes);

  if (leaving_nodes.size() > 0)
  {
    cluster_nodes.insert(cluster_nodes.end(), 
                         leaving_nodes.begin(), 
                         leaving_nodes.end());
  }

  // Shuffle the lists (so the same Chronos node doesn't get queried by
  // all the other nodes at the same time) and remove the local node
  srand(time(NULL));
  std::random_shuffle(cluster_nodes.begin(), cluster_nodes.end()); 
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);
  cluster_nodes.erase(std::remove(cluster_nodes.begin(),
                                  cluster_nodes.end(),
                                  localhost),
                      cluster_nodes.end());

  // Start the scaling operation 
  CL_CHRONOS_START_SCALE.log();
  LOG_DEBUG("Starting scaling operation");

  int nodes_remaining = cluster_nodes.size();

  for (std::vector<std::string>::iterator it = cluster_nodes.begin();
                                          it != cluster_nodes.end();
                                          ++it, --nodes_remaining)
  {
    // Update the number of nodes to query
    std::vector<std::string> updated_values;
    updated_values.push_back(std::to_string(nodes_remaining));
    _nodes_to_query_stat->report_change(updated_values);
    updated_values.clear();

    std::string address;
    int port;
    if (!Utils::split_host_port(*it, address, port))
    {
      // Just use the server as the address.
      address = *it;
      __globals->get_bind_port(port);
    }
    
    std::string server_to_sync = address + ":" + std::to_string(port);
    HTTPCode rc = resynchronise_with_single_node(server_to_sync, 
                                                 cluster_nodes,
                                                 localhost);
    if (rc != HTTP_OK)
    {
      LOG_WARNING("Resynchronisation with node %s failed with rc %d",
                  server_to_sync.c_str(), 
                  rc);
      CL_CHRONOS_RESYNC_ERROR.log(server_to_sync.c_str());
    }
  }

  // The scaling operation is now complete.
  LOG_DEBUG("Finished scaling operation");
  CL_CHRONOS_COMPLETE_SCALE.log();
  std::vector<std::string> finished_value;
  finished_value.push_back(std::to_string(0));
  _nodes_to_query_stat->report_change(finished_value);
}

HTTPCode ChronosInternalConnection::resynchronise_with_single_node(
                             const std::string server_to_sync,
                             std::vector<std::string> cluster_nodes,
                             std::string localhost)
{
  LOG_DEBUG("Querying %s for timers", server_to_sync.c_str());

  // Get the cluster view ID from the global configuration
  std::string cluster_view_id;
  __globals->get_cluster_view_id(cluster_view_id);

  std::string response;
  HTTPCode rc;

  // Loop sending GETs to the server while the response is a 206
  do
  {
    std::map<TimerID, int> delete_map;

    rc = send_get(server_to_sync, 
                  localhost, 
                  PARAM_SYNC_MODE_VALUE_SCALE, 
                  cluster_view_id,
                  MAX_TIMERS_IN_RESPONSE, 
                  response);

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
        LOG_WARNING("Failed to parse document as JSON");
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
            JSON_GET_INT_MEMBER(id_arr, JSON_TIMER_ID, timer_id);

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
            Timer* timer = Timer::from_json_obj(timer_id,
                                                0,
                                                error_str,
                                                replicated_timer,
                                                (rapidjson::Value&)timer_obj);

            if (!timer)
            {
              count_invalid_timers++;
              LOG_INFO("Unable to create timer - error: %s", error_str.c_str());
              continue;
            }
            else if (!replicated_timer)
            {
              count_invalid_timers++;
              LOG_INFO("Unreplicated timer in response - ignoring");
              delete timer; timer = NULL;
              continue;
            }

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
                  store_timer = true;
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
            }
            else
            {
              delete timer; timer = NULL;
            }

            // Finally, note that we processed the timer
            _timers_processed_stat->increment();
          }
          catch (JsonFormatError err)
          {
            // A single entry is badly formatted. This is unexpected but we'll try 
            // to keep going and process the rest of the timers. 
            count_invalid_timers++;
            _invalid_timers_processed_stat->increment();
            LOG_INFO("JSON entry was invalid (hit error at %s:%d)",
                     err._file, err._line);
          }
        }

        // Check if we were able to successfully process any timers - if not
        // then bail out as there's something wrong with the node we're
        // querying
        if ((total_timers != 0) && 
           (count_invalid_timers == total_timers))
        {
          LOG_WARNING("Unable to process any timer entries in GET response");
          rc = HTTP_BAD_REQUEST;
        }
      }
      catch (JsonFormatError err)
      {
        // We've failed to find the Timers array. This suggests that
        // there's something seriously wrong with the node we're trying
        // to query so don't retry
        LOG_WARNING("JSON body didn't contain the Timers array");
        rc = HTTP_BAD_REQUEST;
      }

      // Send a DELETE to all the nodes to update their timer references
      if (delete_map.size() > 0)
      {
        std::string delete_body = create_delete_body(delete_map);
        for (std::vector<std::string>::iterator it = cluster_nodes.begin();
                                                it != cluster_nodes.end();
                                                ++it)
        {
          HTTPCode delete_rc = send_delete(*it, delete_body);
          if (delete_rc != HTTP_ACCEPTED)
          {
            // We've received an error response to the DELETE request. There's
            // not much more we can do here (a timeout will have already 
            // been retried). A failed DELETE won't prevent the scaling operation
            // from finishing, it just means that we'll tell other nodes
            // about timers inefficiently. 
            LOG_INFO("Error response (%d) to DELETE request to %s", 
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
      LOG_WARNING("Error response (%d) to GET request to %s", 
                  rc, 
                  server_to_sync.c_str());
    }
  }
  while (rc == HTTP_PARTIAL_CONTENT);

  return rc;
}

HTTPCode ChronosInternalConnection::send_delete(const std::string server,
                                                const std::string body)
{
  std::string path = "/timers/references";
  HTTPCode rc = _http->send_delete(path, 0, body, server);
  return rc;
}

HTTPCode ChronosInternalConnection::send_get(const std::string server,
                                             const std::string node_for_replicas_param,
                                             const std::string sync_mode_param,
                                             std::string cluster_view_id_param,
                                             int max_timers,
                                             std::string& response)
{
  std::string path = std::string("/timers?") +
                     PARAM_NODE_FOR_REPLICAS + "="  + node_for_replicas_param + ";" +
                     PARAM_SYNC_MODE + "=" + sync_mode_param + ";" +
                     PARAM_CLUSTER_VIEW_ID + "="  + cluster_view_id_param;

  std::string range_header = std::string(HEADER_RANGE) + ":" +
                             std::to_string(MAX_TIMERS_IN_RESPONSE);
  std::vector<std::string> headers;
  headers.push_back(range_header);

  return _http->send_get(path, response, headers, server, 0);
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
      writer.Int(it->first);
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

