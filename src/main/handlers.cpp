#include "handlers.h"
#include <boost/regex.hpp>
#include "json_parse_utils.h"

//static int MAX_TIMERS_IN_RESPONSE = 100;
static const char* const JSON_DELETE_IDS = "IDs";
static const char* const JSON_DELETE_ID = "ID";
static const char* const JSON_DELETE_REPLICA_INDEX = "replica index";

void ControllerTask::run()
{
  // The run function parses what type of message we've recieved, and 
  // passes .. TODO
  std::string path = _req.full_path();
  boost::smatch matches;

  LOG_DEBUG("Path is %s", path.c_str());

  if (_req.method() == htp_method_GET)
  {
    handle_get();
  }
  else if ((path == "/timers") || (path == "/timers/"))
  {
    if (_req.method() != htp_method_POST)
    {
      LOG_DEBUG("Empty timer, but the method wasn't POST");
      send_http_reply(HTTP_BADMETHOD);
    }
    else
    {
      add_or_update_timer(Timer::generate_timer_id(), 0);
    }
  }
  else if (path == "/timers/references")
  {
    if (_req.method() != htp_method_DELETE)
    {
      LOG_DEBUG("Dealing with timer references, but the method wasn't DELETE");
      send_http_reply(HTTP_BADMETHOD);
    }
    else
    {
      handle_delete();
    }
  }
  else if (boost::regex_match(path, matches, boost::regex("/timers/([[:xdigit:]]{16})([[:xdigit:]]{16})")))
  {
    if ((_req.method() != htp_method_PUT) && (_req.method() != htp_method_DELETE))
    {
      LOG_DEBUG("Timer present, but the method wasn't PUT or DELETE");
      send_http_reply(HTTP_BADMETHOD);
    }
    else
    {
      TimerID timer_id = std::stoul(matches[1].str(), NULL, 16);
      uint64_t replica_hash = std::stoull(matches[2].str(), NULL, 16);
      add_or_update_timer(timer_id, replica_hash);
    }
  }
  else
  {
    LOG_DEBUG("Invalid, or timer present but badly formatted");
    send_http_reply(HTTP_NOT_FOUND);
  }

  delete this;
}

void ControllerTask::add_or_update_timer(int timer_id, int replica_hash)
{
  Timer* timer = NULL;
  bool replicated_timer;

  if (_req.method() == htp_method_DELETE)
  {
    // Replicated deletes are implemented as replicated tombstones so no DELETE
    // can be a replication request.
    replicated_timer = false;
    timer = Timer::create_tombstone(timer_id, replica_hash);
  }
  else
  {
    std::string body = _req.get_rx_body();
    std::string error_str;
    timer = Timer::from_json(timer_id,
                             replica_hash,
                             body,
                             error_str,
                             replicated_timer);

    if (!timer)
    {
      LOG_DEBUG("Unable to create timer");
      send_http_reply(HTTP_BAD_REQUEST);
      return;
    }
  }

  LOG_DEBUG("Accepted timer definition, timer is%s a replica", replicated_timer ? "" : " not");

  // Now we have a valid timer object, reply to the HTTP request.
  _req.add_header("Location", timer->url());
  send_http_reply(HTTP_OK);

  // Replicate the timer to the other replicas if this is a client request
  if (!replicated_timer)
  {
    _cfg->_replicator->replicate(timer);
  }

  // If the timer belongs to the local node, store it. Otherwise, turn it into
  // a tombstone.
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);

  if (!timer->is_local(localhost))
  {
    timer->become_tombstone();
  }

  _cfg->_handler->add_timer(timer);

  // The store takes ownership of the timer.
  timer = NULL;
}

void ControllerTask::handle_delete()
{
  // TODO Check the request is valid - should be valid JSON in the body
  std::string body = _req.get_rx_body(); 
  rapidjson::Document doc;
  doc.Parse<0>(body.c_str());

  if (doc.HasParseError())
  {
    // TODO log
    send_http_reply(HTTP_BAD_REQUEST);
    return;
  }


  // Now loop through the body, pulling out the IDs/replica numbers
  // The JSON body should have the format TODO update to be correct
  // {"IDs": [{"id1", replica_number},
  //          {"id2", replica_number}, 
  //          ... ]
  // }
  try
  {
    JSON_ASSERT_CONTAINS(doc, JSON_DELETE_IDS);
    JSON_ASSERT_ARRAY(doc[JSON_DELETE_IDS]);
    const rapidjson::Value& ids_arr = doc[JSON_DELETE_IDS];
   
    // The request is valid, so respond with a 202. Now loop through the 
    // the body and update the replica trackers. 
    send_http_reply(HTTP_ACCEPTED);

    for (rapidjson::Value::ConstValueIterator ids_it = ids_arr.Begin();
         ids_it != ids_arr.End();
         ++ids_it)
    {
      try
      { 
        TimerID id;
        int replica_index;
        JSON_GET_INT_MEMBER(*ids_it, JSON_DELETE_ID, id);
        JSON_GET_INT_MEMBER(*ids_it, JSON_DELETE_REPLICA_INDEX, replica_index);

        _cfg->_store->update_replica_tracker(id, replica_index);
      }
      catch (JsonFormatError err)
      {
        LOG_DEBUG("TODO2"); // TODO ENT
      }
    }
  }
  catch (JsonFormatError err)
  {
    LOG_DEBUG("TODO1"); // TODO ENT
    send_http_reply(HTTP_BAD_REQUEST);
  }
}

void ControllerTask::handle_get()
{
  // Check the request is valid. It must have requesting-node 
  // and sync-mode set, sync-mode must be SCALE (this will be
  // extended later) and request-node must correspond to a node
  // in the Chronos cluster (it can be a leaving node).
  std::string requesting_node = _req.param("requesting-node");
  std::string sync_mode = _req.param("sync-mode");  

  if ((requesting_node == "") || (sync_mode == ""))
  {
    LOG_DEBUG("GET request doesn't have mandatory parameters");
    send_http_reply(HTTP_BAD_REQUEST);
    return;
  }

  if (!node_is_in_cluster(requesting_node))
  {
    LOG_DEBUG("The request node isn't a Chronos node: %s", requesting_node.c_str());
    send_http_reply(HTTP_BAD_REQUEST);
    return;
  }

  if (sync_mode == "SCALE") 
  {
    std::string max_timers_from_req = _req.header("Range");

    LOG_DEBUG("range %s", max_timers_from_req.c_str());
    int max_timers_to_get = atoi(max_timers_from_req.c_str());

    std::string get_response;
    HTTPCode rc = _cfg->_store->get_timers_to_recover(requesting_node, 
                                                      get_response,
                                                      max_timers_to_get);
    _req.add_content(get_response);
    
    if (rc == HTTP_PARTIAL_CONTENT)
    {
      _req.add_header("Content-Range", max_timers_from_req);
    }
    
    send_http_reply(rc);
  }
  else
  {
    LOG_DEBUG("Sync mode is unsupported: %s", sync_mode.c_str());
    send_http_reply(HTTP_BAD_REQUEST);
  }
}

// TODO move this to a utils class
bool ControllerTask::node_is_in_cluster(std::string requesting_node)
{
  // Check the requesting node is a Chronos node
  std::vector<std::string> cluster;
  __globals->get_cluster_addresses(cluster);

  bool node_in_cluster = false;

  for (std::vector<std::string>::iterator it = cluster.begin();
                                          it != cluster.end();
                                          ++it)
  {
    if (*it == requesting_node)
    {
      LOG_DEBUG("Found requesting node in current nodes: %s", requesting_node.c_str());
      node_in_cluster = true;
      break;
    }
  }

  if (!node_in_cluster)
  {
    std::vector<std::string> leaving;
    __globals->get_cluster_leaving_addresses(leaving);

    for (std::vector<std::string>::iterator it = leaving.begin();
                                            it != leaving.end();
                                            ++it)
    {
      if (*it == requesting_node)
      {
        LOG_DEBUG("Found requesting node in current nodes: %s", requesting_node.c_str());
        node_in_cluster = true;
        break;
      }
    }
  }

  return node_in_cluster;
}
