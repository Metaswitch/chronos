#include "controller.h"
#include "timer.h"
#include "globals.h"
#include "log.h"

#include "murmur/MurmurHash3.h"

#include <boost/regex.hpp>

Controller::Controller(Replicator* replicator,
                       TimerHandler* handler) :
                       _replicator(replicator),
                       _handler(handler)
{
}

Controller::~Controller()
{
}

void Controller::handle_request(struct evhttp_request* req)
{
  // Do our own path parsing to check the request is to a known path:
  //
  // /timers
  // /timers/
  // /timers/<timerid>
  const char *uri = evhttp_request_get_uri(req);
  struct evhttp_uri* decoded = evhttp_uri_parse(uri);
  if (!decoded)
  {
    send_error(req, HTTP_BADREQUEST, "Requested URI is unparseable");
    return;
  }

  const char* encoded_path = evhttp_uri_get_path(decoded);
  if (!encoded_path)
  {
    encoded_path = "/";
  }

  size_t path_len;
  char* path_str = evhttp_uridecode(encoded_path, 0, &path_len);
  if (!path_str)
  {
    send_error(req, HTTP_BADREQUEST, "Requested path is unparseable");
    return;
  }

  std::string path(path_str, path_len);

  // At this point, we're done with the URI and can free the C objects (we'll use
  // the string from now on).
  free(path_str);
  path_str = NULL;
  evhttp_uri_free(decoded);
  decoded = NULL;

  LOG_DEBUG("Request: %s", path.c_str());

  // Also need to check the user has supplied a valid method:
  //
  //  * POST to the collection
  //  * PUT to a specific ID
  //  * DELETE to a specific ID
  evhttp_cmd_type method = evhttp_request_get_command(req);

  boost::smatch matches;
  TimerID timer_id;
  uint64_t replica_hash = 0;
  if ((path == "/timers") || (path == "/timers/"))
  {
    if (method != EVHTTP_REQ_POST)
    {
      send_error(req, HTTP_BADMETHOD, NULL);
      return;
    }
    timer_id = Timer::generate_timer_id();
  }
  else if (boost::regex_match(path, matches, boost::regex("/timers/([[:xdigit:]]{16})([[:xdigit:]]{16})")))
  {
    if ((method != EVHTTP_REQ_PUT) && (method != EVHTTP_REQ_DELETE))
    {
      send_error(req, HTTP_BADMETHOD, NULL);
      return;
    }
    timer_id = std::stoul(matches[1].str(), NULL, 16);
    replica_hash = std::stoull(matches[2].str(), NULL, 16);
  }
  else
  {
    send_error(req, HTTP_NOTFOUND, NULL);
    return;
  }

  // At this point, the ReqURI has been parsed and validated and we've got the
  // ID for the timer worked out.  Now, create the timer object from the body,
  // for a DELETE request, we'll create a tombstone record instead.
  Timer* timer = NULL;
  bool replicated_timer;
  if (method == EVHTTP_REQ_DELETE)
  {
    // Replicated deletes are implemented as replicated tombstones so no DELETE
    // can be a replication request.
    replicated_timer = false;
    timer = Timer::create_tombstone(timer_id, replica_hash);
  }
  else
  {
    std::string body = get_req_body(req);
    std::string error_str;
    timer = Timer::from_json(timer_id, replica_hash, body, error_str, replicated_timer);
    if (!timer)
    {
      send_error(req, HTTP_BADREQUEST, error_str.c_str());
      return;
    }
  }

  LOG_DEBUG("Accepted timer definition, timer is%s a replica",
            replicated_timer ? "" : " not");

  // Now we have a valid timer object, reply to the HTTP request.
  evhttp_add_header(evhttp_request_get_output_headers(req),
                    "Location", timer->url("localhost").c_str());
  evhttp_send_reply(req, 200, "OK", NULL);

  // Replicate the timer to the other replicas if this is a client request
  if (!replicated_timer)
  {
    _replicator->replicate(timer);
  }

  // If the timer belongs to the local node, store it. Otherwise, turn it into
  // a tombstone.
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);

  if (!timer->is_local(localhost))
  {
    timer->become_tombstone();
  }

  _handler->add_timer(timer);
  timer = NULL;
}

void Controller::controller_cb(struct evhttp_request* req, void* controller)
{
  ((Controller*)controller)->handle_request(req);
}

void Controller::controller_ping_cb(struct evhttp_request* req, void* controller)
{
  evhttp_send_reply(req, 200, "OK", NULL);
}

/*****************************************************************************/
/* PRIVATE FUNCTIONS                                                         */
/*****************************************************************************/

void Controller::send_error(struct evhttp_request* req, int error, const char* reason)
{
  LOG_ERROR("Rejecting request with %d %s", error, reason);
  evhttp_send_error(req, error, reason);
}

std::string Controller::get_req_body(struct evhttp_request* req)
{
  struct evbuffer* evbuf;
  std::string rc;
  evbuf = evhttp_request_get_input_buffer(req);
  while (evbuffer_get_length(evbuf))
  {
    int nbytes;
    char* buf[1024];
    nbytes = evbuffer_remove(evbuf, buf, sizeof(buf));
    if (nbytes > 0)
    {
      rc.append((const char*)buf, (size_t)nbytes);
    }
  }
  return rc;
}
