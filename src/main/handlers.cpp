#include "handlers.h"
#include <boost/regex.hpp>

void ControllerTask::run()
{
  HTTPCode rc = parse_request();

  if (rc != HTTP_OK)
  {
    LOG_DEBUG("Timer request rejected with %d", rc);
    send_http_reply(rc);
    delete this;
    return;
  }

  // At this point, the ReqURI has been parsed and validated and we've got the
  // ID for the timer worked out.  Now create the timer object from the body -
  // for a DELETE request, we'll create a tombstone record instead.
  Timer* timer = NULL;
  bool replicated_timer;
  if (_req.method() == htp_method_DELETE)
  {
    // Replicated deletes are implemented as replicated tombstones so no DELETE
    // can be a replication request.
    replicated_timer = false;
    timer = Timer::create_tombstone(_timer_id, _replica_hash);
  }
  else
  {
    std::string body = _req.get_rx_body();
    std::string error_str;
    timer = Timer::from_json(_timer_id, 
                             _replica_hash, 
                             body, 
                             error_str, 
                             replicated_timer);

    if (!timer)
    {
      LOG_DEBUG("Unable to create timer");
      send_http_reply(HTTP_BAD_RESULT);
      delete this;
      return;
    }
  }

  LOG_DEBUG("Accepted timer definition, timer is%s a replica",
            replicated_timer ? "" : " not");

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
  timer = NULL;
}

HTTPCode ControllerTask::parse_request()
{
  std::string path = _req.path();
  boost::smatch matches;

  LOG_DEBUG("Path is %s", path.c_str());

  if ((path == "/timers") || (path == "/timers/"))
  {
    if (_req.method() != htp_method_POST)
    {
      LOG_DEBUG("Empty timer, but the method wasn't POST");
      return HTTP_BADMETHOD;
    }
    else
    {
      _timer_id = Timer::generate_timer_id();
    }
  }
  else if (boost::regex_match(path, matches, boost::regex("/timers/([[:xdigit:]]{16})([[:xdigit:]]{16})")))
  {
    if ((_req.method() != htp_method_PUT) && (_req.method() != htp_method_DELETE))
    {
      LOG_DEBUG("Timer present, but the method wasn't PUT or DELETE");
      return HTTP_BADMETHOD;
    }
    else
    {
      _timer_id = std::stoul(matches[1].str(), NULL, 16);
      _replica_hash = std::stoull(matches[2].str(), NULL, 16);
    }
  }
  else
  {
    LOG_DEBUG("Timer present, but badly formatted");
    return HTTP_NOT_FOUND;
  }

  return HTTP_OK;
}
