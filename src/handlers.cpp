/**
 * @file handlers.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
  // For a PUT or a DELETE the URL should be of the format
  // <timer_id>-<replication_factor>
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
  bool replicated_timer = false;
  bool gr_replicated_timer = false;

  if (_req.method() == htp_method_DELETE)
  {
    // Replicated deletes are implemented as replicated tombstones so no DELETE
    // can be a replication request - it must have come from the client so we
    // should replicate it ourselves (both within site and cross-site).
    timer = Timer::create_tombstone(timer_id, replica_hash, replication_factor);
  }
  else
  {
    // Create a timer from the JSON body. This also works out whether the
    // timer has already been replicated within/cross-site.
    std::string body = _req.get_rx_body();
    std::string error_str;
    timer = Timer::from_json(timer_id,
                             replication_factor,
                             replica_hash,
                             body,
                             error_str,
                             replicated_timer,
                             gr_replicated_timer);

    if (!timer)
    {
      TRC_ERROR("Unable to create timer - %s", error_str.c_str());
      _req.add_content(error_str);
      send_http_reply(HTTP_BAD_REQUEST);
      return;
    }
  }

  TRC_DEBUG("Timer accepted: %s replicating within-site, %s replicating cross-site",
            replicated_timer ? "does not need" : "needs",
            gr_replicated_timer ? "does not need" : "needs");

  // Now we have a valid timer object, reply to the HTTP request.
  _req.add_header("Location", timer->url());
  send_http_reply(HTTP_OK);

  // Replicate the timer to the other replicas within the site if this is the
  // first Chronos in this site to handle the request
  if (!replicated_timer)
  {
    _cfg->_replicator->replicate(timer);

    // Replicate the timer cross site if this is the first Chronos in this
    // deployment to handle the request, and the GR replicator exists (it will
    // only exist if the system has been configured to replicate across sites).
    if ((_cfg->_gr_replicator != NULL) && (!gr_replicated_timer))
    {
      _cfg->_gr_replicator->replicate(timer);
    }
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
  uint32_t time_from = Utils::get_time();

  if (time_from_str != "")
  {
    time_from += atoi(time_from_str.c_str());
  }

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
