/**
 * @file timer_helper.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "timer_helper.h"

Timer* default_timer(TimerID id)
{
  // Start the timer right now.
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  // Add a single timer to the store
  Timer* timer = new Timer(id, 100000, 100000);
  timer->start_time_mono_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
  timer->sequence_number = 0;
  timer->replicas = std::vector<std::string>(1, "10.0.0.1:9999");
  timer->sites = std::vector<std::string>(1, "local_site_name");
  timer->sites.push_back("remote_site_1_name");
  timer->tags = std::map<std::string, uint32_t> {{"TAG" + std::to_string(id), 1}};
  timer->callback_url = "http://localhost:80/callback" + std::to_string(id);
  timer->callback_body = "stuff stuff stuff";
  timer->_replication_factor = 1;
  timer->cluster_view_id = "cluster-view-id";

  return timer;
}
