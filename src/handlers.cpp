/**
 * @file handlers.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015  Metaswitch Networks Ltd
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

#include "handlers.h"
#include <boost/regex.hpp>
#include "json_parse_utils.h"
#include "constants.h"

void ControllerTask::run()
{
  std::string path = _req.full_path();
  TRC_DEBUG("Path is %s", path.c_str());

  boost::smatch matches;

  if (_req.method() == htp_method_GET)
  {
    handle_get();
  }
  else if ((path == "/timers") || (path == "/timers/"))
  {
    if (_req.method() != htp_method_POST)
    {
      TRC_DEBUG("Empty timer, but the method wasn't POST");
      send_http_reply(HTTP_BADMETHOD);
    }
    else
    {
      add_or_update_timer(Timer::generate_timer_id(), 0, 0);
    }
  }
  else if (path == "/timers/references")
  {
    if (_req.method() != htp_method_DELETE)
    {
      TRC_DEBUG("Dealing with timer references, but the method wasn't DELETE");
      send_http_reply(HTTP_BADMETHOD);
    }
    else
    {
      handle_delete();
    }
  }
  // For a PUT or a DELETE the URL should be of the format
  // <timer_id>-<replication_factor> or <timer_id><replica_hash>
  else if (boost::regex_match(path, matches, boost::regex("/timers/([[:xdigit:]]{16})([[:xdigit:]]{16})")))
  {
    if ((_req.method() != htp_method_PUT) && (_req.method() != htp_method_DELETE))
    {
      TRC_DEBUG("Timer present, but the method wasn't PUT or DELETE");
      send_http_reply(HTTP_BADMETHOD);
    }
    else
    {
      TimerID timer_id = std::stoull(matches[1].str(), NULL, 16);
      uint64_t replica_hash = std::stoull(matches[2].str(), NULL, 16);
      add_or_update_timer(timer_id, 0, replica_hash);
    }
  }
  else if (boost::regex_match(path, matches, boost::regex("/timers/([[:xdigit:]]{16})-([[:digit:]]+)")))
  {
    if ((_req.method() != htp_method_PUT) && (_req.method() != htp_method_DELETE))
    {
      TRC_DEBUG("Timer present, but the method wasn't PUT or DELETE");
      send_http_reply(HTTP_BADMETHOD);
    }
    else
    {
      TimerID timer_id = std::stoull(matches[1].str(), NULL, 16);
      uint32_t replication_factor = std::stoull(matches[2].str(), NULL);
      add_or_update_timer(timer_id, replication_factor, 0);
    }
  }
  else
  {
    TRC_DEBUG("Invalid request, or timer present but badly formatted");
    send_http_reply(HTTP_NOT_FOUND);
  }

  delete this;
}

void ControllerTask::add_or_update_timer(TimerID timer_id,
                                         uint32_t replication_factor,
                                         uint64_t replica_hash)
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
                             replication_factor,
                             replica_hash,
                             body,
                             error_str,
                             replicated_timer);

    if (!timer)
    {
      TRC_ERROR("Unable to create timer - %s", error_str.c_str());
      _req.add_content(error_str);
      send_http_reply(HTTP_BAD_REQUEST);
      return;
    }
  }

  TRC_DEBUG("Accepted timer definition, timer is%s a replica", replicated_timer ? "" : " not");

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
  // Check the request has a valid JSON body
  std::string body = _req.get_rx_body();
  rapidjson::Document doc;
  doc.Parse<0>(body.c_str());

  if (doc.HasParseError())
  {
    TRC_INFO("Failed to parse document as JSON");
    send_http_reply(HTTP_BAD_REQUEST);
    return;
  }

  // Now loop through the body, pulling out the IDs/replica numbers
  // The JSON body should have the format:
  //  {"IDs": [{"ID": 123, "ReplicaIndex": 0},
  //           {"ID": 456, "ReplicaIndex": 2},
  //          ...]
  // The replica_index is zero-indexed (so the primary replica has an
  // index of 0).
  try
  {
    JSON_ASSERT_CONTAINS(doc, JSON_IDS);
    JSON_ASSERT_ARRAY(doc[JSON_IDS]);
    const rapidjson::Value& ids_arr = doc[JSON_IDS];

    // The request is valid, so respond with a 202. Now loop through the
    // the body and update the replica trackers.
    send_http_reply(HTTP_ACCEPTED);

    for (rapidjson::Value::ConstValueIterator ids_it = ids_arr.Begin();
         ids_it != ids_arr.End();
         ++ids_it)
    {
      try
      {
        TimerID timer_id;
        int replica_index;
        JSON_GET_INT_64_MEMBER(*ids_it, JSON_ID, timer_id);
        JSON_GET_INT_MEMBER(*ids_it, JSON_REPLICA_INDEX, replica_index);

        // Update the timer's replica_tracker to show that the replicas
        // at level 'replica_index' and higher have been informed
        // about the timer. This will tombstone the timer if all
        // replicas have been informed.
        _cfg->_handler->update_replica_tracker_for_timer(timer_id,
                                                         replica_index);
      }
      catch (JsonFormatError& err)
      {
        TRC_INFO("JSON entry was invalid (hit error at %s:%d)",
                  err._file, err._line);
      }
    }
  }
  catch (JsonFormatError& err)
  {
    TRC_INFO("JSON body didn't contain the IDs array");
    send_http_reply(HTTP_BAD_REQUEST);
  }
}

void ControllerTask::handle_get()
{
  // Check the request is valid. It must have the node-for-replicas
  // and cluster-view-id parameters set, the request-node
  // must correspond to a node in the Chronos cluster (it can be a
  // leaving node), and the cluster-view-id request must correspond to
  // the receiving nodes view of the cluster configuration
  std::string node_for_replicas = _req.param(PARAM_NODE_FOR_REPLICAS);
  std::string cluster_view_id = _req.param(PARAM_CLUSTER_VIEW_ID);

  if ((node_for_replicas == "") ||
      (cluster_view_id == ""))
  {
    TRC_INFO("GET request doesn't have mandatory parameters");
    send_http_reply(HTTP_BAD_REQUEST);
    return;
  }

  std::string global_cluster_view_id;
  __globals->get_cluster_view_id(global_cluster_view_id);

  if (cluster_view_id != global_cluster_view_id)
  {
    TRC_INFO("GET request is for an out of date cluster (%s and %s)",
             cluster_view_id.c_str(),
             global_cluster_view_id.c_str());
    send_http_reply(HTTP_BAD_REQUEST);
    return;
  }

  if (!node_is_in_cluster(node_for_replicas))
  {
    TRC_DEBUG("The request node isn't a Chronos node: %s",
              node_for_replicas.c_str());
    send_http_reply(HTTP_BAD_REQUEST);
    return;
  }

  std::string max_timers_from_req = _req.header(HEADER_RANGE);
  int max_timers_to_get = atoi(max_timers_from_req.c_str());
  TRC_DEBUG("Range value is %d", max_timers_to_get);

  std::string time_from_str = _req.param(PARAM_TIME_FROM);
  uint64_t time_from = atoi(time_from_str.c_str());
  TRC_DEBUG("Time-from value is %d", time_from);

  std::string get_response;
  HTTPCode rc = _cfg->_handler->get_timers_for_node(node_for_replicas,
                                                    max_timers_to_get,
                                                    cluster_view_id,
                                                    time_from,
                                                    get_response);
  _req.add_content(get_response);

  if (rc == HTTP_PARTIAL_CONTENT)
  {
    _req.add_header(HEADER_CONTENT_RANGE, max_timers_from_req);
  }

  send_http_reply(rc);
}

bool ControllerTask::node_is_in_cluster(std::string node_for_replicas)
{
  // Check the requesting node is a Chronos node
  std::vector<std::string> cluster;
  __globals->get_cluster_staying_addresses(cluster);

  bool node_in_cluster = false;

  for (std::vector<std::string>::iterator it = cluster.begin();
                                          it != cluster.end();
                                          ++it)
  {
    if (*it == node_for_replicas)
    {
      TRC_DEBUG("Found requesting node in current nodes: %s",
                node_for_replicas.c_str());
      node_in_cluster = true;
      break;
    }
  }

  if (!node_in_cluster)
  {
    std::vector<std::string> joining;
    __globals->get_cluster_joining_addresses(joining);

    for (std::vector<std::string>::iterator it = joining.begin();
                                            it != joining.end();
                                            ++it)
    {
      if (*it == node_for_replicas)
      {
        TRC_DEBUG("Found requesting node in joining nodes: %s",
                  node_for_replicas.c_str());
        node_in_cluster = true;
        break;
      }
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
      if (*it == node_for_replicas)
      {
        TRC_DEBUG("Found requesting node in leaving nodes: %s",
                  node_for_replicas.c_str());
        node_in_cluster = true;
        break;
      }
    }
  }

  return node_in_cluster;
}
