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

ChronosInternalConnection::ChronosInternalConnection(HttpResolver* resolver, 
                                                     TimerHandler* handler, 
                                                     Replicator* replicator) :
  _http(new HttpConnection("",
                           false,
                           resolver,
                           SASEvent::HttpLogLevel::DETAIL,
                           NULL)),
  _handler(handler),
  _replicator(replicator)
{
}

ChronosInternalConnection::~ChronosInternalConnection()
{
  delete _http; _http = NULL;
}

HTTPCode ChronosInternalConnection::send_delete(const std::string server,
                                                const std::string body)
{
  std::string path = "/timers/references";
  return _http->send_delete(path, 0, body, server);
}

HTTPCode ChronosInternalConnection::send_get(const std::string server,
                                             const std::string request_node_param,
                                             const std::string sync_mode_param,
                                             int max_timers,
                                             std::string& response)
{
  std::string path = std::string("/timers?") + 
                     PARAM_REQUESTING_NODE + "="  + request_node_param + ";" +
                     PARAM_SYNC_MODE + "=" + sync_mode_param;

  std::string range_header = std::string(HEADER_RANGE) + ":" + 
                             std::to_string(MAX_TIMERS_IN_RESPONSE);
  std::vector<std::string> headers;
  headers.push_back(range_header);

  return _http->send_get(path, response, headers, server, 0);
}

HTTPCode ChronosInternalConnection::trigger_move_for_one_server(const std::string server_to_ask)
{
  // Get the current node and any leaving nodes from 
  // the global configuration
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);
  std::vector<std::string> leaving_nodes;
  __globals->get_cluster_leaving_addresses(leaving_nodes);

  std::string response;
  HTTPCode rc;

  // Loop sending GETs to the server while the response is a 206
  do
  {
    std::map<TimerID, int> delete_map;

    rc = send_get(server_to_ask, 
                  localhost, 
                  PARAM_SYNC_MODE_VALUE_SCALE, 
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
        LOG_INFO("Failed to parse document as JSON");
        rc = HTTP_BAD_REQUEST;
        break;
      }

      try
      {
        JSON_ASSERT_CONTAINS(doc, JSON_TIMERS);
        JSON_ASSERT_ARRAY(doc[JSON_TIMERS]);
        const rapidjson::Value& ids_arr = doc[JSON_TIMERS];

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
              LOG_INFO("Unable to create timer - error: %s", error_str.c_str());
              continue;
            }
            else if (!replicated_timer)
            {
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
              // replication to any node that used to be a replica and now 
              // isn't, and isn't a leaving node. 
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
                  bool leaving_node = get_replica_presence(*it, 
                                                           leaving_nodes);

                  if ((!old_rep_in_new_rep) && 
                      (!leaving_node))
                  {
                    Timer* timer_copy = new Timer(*timer);
                    timer_copy->become_tombstone();
                    _replicator->replicate_timer_to_node(timer_copy, *it);
                    delete timer_copy; timer_copy = NULL;
                  }
                }
              }
            }

            // Finally, add the timer to the store if we can. This is done 
            // last so we don't invalidate the pointer to the timer.
            if (store_timer)
            {
              _handler->add_timer(timer);
            }
            else
            {
              delete timer; timer = NULL;
            }
          }
          catch (JsonFormatError err)
          {
            LOG_INFO("JSON entry was invalid (hit error at %s:%d)",
                     err._file, err._line);
          }
        }
      }
      catch (JsonFormatError err)
      {
        rc = HTTP_BAD_REQUEST;
        LOG_INFO("JSON body didn't contain the Timers array");
      }

      // Finally, send a DELETE to all the leaving nodes to update 
      // their timer references
      std::string delete_body = create_delete_body(delete_map);
      for (std::vector<std::string>::iterator it = leaving_nodes.begin();
                                              it != leaving_nodes.end();
                                              ++it)
       
      {
        HTTPCode delete_rc = send_delete(*it, delete_body);
        if (delete_rc != HTTP_ACCEPTED)
        {
          LOG_INFO("Error response to DELETE request");
        }
      }
    }
    else
    {
      LOG_INFO("Error response to GET request");
    }
  }
  while (rc == HTTP_PARTIAL_CONTENT);

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
      LOG_DEBUG("Found the current node in the replica list");
      return true;
    }
  }

  return false;
}

