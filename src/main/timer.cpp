#include "timer.h"
#include "rapidjson/document.h"

#include <sstream>
#include <boost/format.hpp>

Timer::Timer(TimerID id,
             unsigned long long start_time,
             unsigned int interval,
             unsigned int repeat_for,
             unsigned int sequence_number,
             std::vector<std::string> replicas,
             std::string callback_url,
             std::string callback_body) :
  id(id),
  start_time(start_time),
  interval(interval),
  repeat_for(repeat_for),
  sequence_number(sequence_number),
  replication_factor(replicas.size()),
  replicas(replicas),
  callback_url(callback_url),
  callback_body(callback_body)
{
}
                   
Timer::~Timer()
{
}

// Returns the next pop time in ms.
unsigned long long Timer::next_pop_time()
{
  // TODO add replication skew
  return start_time + ((sequence_number + 1) * interval);
}

// Construct a timespec describing the next pop time.
void Timer::next_pop_time(struct timespec& ts)
{
  unsigned long long pop_time = next_pop_time();
  ts.tv_sec = pop_time / 1000;
  ts.tv_nsec = (pop_time % 1000) * 1000000;
}

// Create the timer's URL from a given hostname.
std::string Timer::url(std::string host)
{
  std::stringstream ss;
  ss << "http://" << host << "/timers/" << id;
  for (auto it = replicas.begin(); it != replicas.end(); it++)
  {
    ss << "-" << (*it);
  }
  return ss.str();
}

// Render the timer as JSON to be used in an HTTP request body.
std::string Timer::to_json()
{
  std::stringstream ss;
  ss << "{\"timing\":{\"start-at\":\""
     << start_time
     << "\",\"sequence-number\":\""
     << sequence_number
     << "\",\"interval\":\""
     << interval
     << "\",\"repeat-for\":\""
     << repeat_for
     << "\"},\"callback\":{\"http\":{\"uri\":\""
     << callback_url
     << "\",\"opaque\":\""
     << callback_body
     << "\"}},\"reliability\":{\"replicas\":[";
  for (auto it = replicas.begin(); it != replicas.end(); it++)
  {
    ss << "\"" << *it << "\"";
    if (it + 1 != replicas.end())
    {
      ss << ",";
    }
  }
  ss << "]}}";
  return ss.str();
}

bool Timer::is_local(std::string host)
{
  return (std::find(replicas.begin(), replicas.end(), host) != replicas.end());
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
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  static const uint32_t instance_id_bits = 5;
  static const uint32_t deployment_id_bits = 5;
  static const uint32_t sequence_bits = 12;

  static const uint32_t instance_id_shift = sequence_bits;
  static const uint32_t deployment_id_shift = instance_id_shift + instance_id_bits;
  static const uint32_t timestamp_shift = deployment_id_shift + deployment_id_bits;

  static const uint32_t sequence_mask = 0xFFFFFFFF ^ (0xFFFFFFFF << sequence_bits);
  static uint32_t sequence_number = 0;
  static uint32_t last_timestamp = 0;

  uint32_t timestamp = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
  if (timestamp == last_timestamp)
  {
    sequence_number++;
  }
  else
  {
    sequence_number = 0;
    last_timestamp = timestamp;
  }
  
  uint32_t rc = (timestamp << timestamp_shift) | 
                (Timer::deployment_id << deployment_id_shift) |
                (Timer::instance_id << instance_id_shift) |
                (sequence_number & sequence_mask);
  return rc;
}

Timer* Timer::create_tombstone(TimerID id)
{
  return new Timer(true, id);
}

#define JSON_PARSE_ERROR(STR) {                                               \
  error = (STR);                                                              \
  delete timer;                                                               \
  return NULL;                                                                \
}

#define JSON_ASSERT_OBJECT(NODE, NODE_NAME) {                                 \
  if (!(NODE).IsObject())                                                     \
    JSON_PARSE_ERROR((NODE_NAME " should be an object"));                     \
}

#define JSON_ASSERT_INTEGER(NODE, NODE_NAME) {                                \
  if (!(NODE).IsInt())                                                        \
    JSON_PARSE_ERROR((NODE_NAME " should be an integer"));                    \
}

#define JSON_ASSERT_STRING(NODE, NODE_NAME) {                                 \
  if (!(NODE).IsString())                                                     \
    JSON_PARSE_ERROR((NODE_NAME " should be a string"));                      \
}

#define JSON_ASSERT_ARRAY(NODE, NODE_NAME) {                                  \
  if (!(NODE).IsArray())                                                      \
    JSON_PARSE_ERROR((NODE_NAME " should be an array"));                      \
}

#define JSON_ASSERT_CONTAINS(NODE, NODE_NAME, ELEM) {                         \
  if (!(NODE).HasMember(ELEM))                                                \
    JSON_PARSE_ERROR(("Couldn't find '" ELEM "' in '" NODE_NAME "'"));        \
}

// Create a Timer object from the JSON representation. 
//
// @param id - The unique identity for the timer (see generate_timer_id() above).
// @param replicas - The previously calculated list of replicas for this timer (may be blank for a new timer).
// @param json - The JSON representation of the timer.
// @param error - This will be populated wiht a descriptive error string if required.
Timer* Timer::from_json(TimerID id, std::vector<std::string> replicas, std::string json, std::string& error)
{
  Timer* timer = new Timer(id);
  rapidjson::Document doc;
  doc.Parse<0>(json.c_str());
  if (doc.HasParseError())
  {
    JSON_PARSE_ERROR(boost::str(boost::format("Failed to parse JSON body, offset: %lu - %s") % doc.GetErrorOffset() % doc.GetParseError()));
  }

  if (!doc.HasMember("timing"))
    JSON_PARSE_ERROR(("Couldn't find the 'timing' node in the JSON"));
  if (!doc.HasMember("callback"))
    JSON_PARSE_ERROR(("Couldn't find the 'callback' node in the JSON"));
  if (!doc.HasMember("reliability"))
    JSON_PARSE_ERROR(("Couldn't find the 'reliability' node in the JSON"));

  // Parse out the timing block
  rapidjson::Value& timing = doc["timing"];
  
  JSON_ASSERT_OBJECT(timing, "timing");
  JSON_ASSERT_CONTAINS(timing, "timing", "interval");
  JSON_ASSERT_CONTAINS(timing, "timing", "repeat-for");
  
  rapidjson::Value& interval = timing["interval"];
  rapidjson::Value& repeat_for = timing["repeat-for"];

  JSON_ASSERT_INTEGER(interval, "interval");
  JSON_ASSERT_INTEGER(repeat_for, "repeat-for");

  if (timing.HasMember("start-time"))
  {
    rapidjson::Value& start_time = timing["start-time"];
    JSON_ASSERT_INTEGER(start_time, "start-time");
    timer->start_time = start_time.GetInt();
  }
  else
  {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    timer->start_time = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
  }

  timer->interval = interval.GetInt();
  timer->repeat_for = repeat_for.GetInt();

  if (timing.HasMember("sequence-number"))
  {
    rapidjson::Value& sequence_number = timing["sequence-number"];
    JSON_ASSERT_INTEGER(sequence_number, "sequence-number");
    timer->sequence_number = sequence_number.GetInt();
  }

  // Parse out the 'callback' block
  rapidjson::Value& callback = doc["callback"];

  JSON_ASSERT_OBJECT(callback, "callback");
  JSON_ASSERT_CONTAINS(callback, "callback", "http");

  rapidjson::Value& http = callback["http"];

  JSON_ASSERT_OBJECT(http, "http");
  JSON_ASSERT_CONTAINS(http, "http", "uri");
  JSON_ASSERT_CONTAINS(http, "http", "opaque");

  rapidjson::Value& uri = http["uri"];
  rapidjson::Value& opaque = http["opaque"];

  JSON_ASSERT_STRING(uri, "uri");
  JSON_ASSERT_STRING(opaque, "opaque");

  timer->callback_url = std::string(uri.GetString(), uri.GetStringLength());
  timer->callback_body = std::string(opaque.GetString(), opaque.GetStringLength());

  // Parse out the 'reliability' block
  rapidjson::Value& reliability = doc["reliability"];
  
  JSON_ASSERT_OBJECT(reliability, "reliability");

  if (replicas.empty())
  {
    if (reliability.HasMember("replication-factor"))
    {
      rapidjson::Value& replication_factor = reliability["replication-factor"];
      JSON_ASSERT_INTEGER(replication_factor, "replication-factor");
      timer->replication_factor = replication_factor.GetInt();
    }
    else
    {
      // Default replication factor is 2.
      timer->replication_factor = 2;
    }

    // The controller will allocate replicas for us based on the replication factor.
  }
  else
  {
    timer->replicas = replicas;
  }
  
  return timer;
}
