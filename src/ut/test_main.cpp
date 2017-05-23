/**
 * @file main.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <curl/curl.h>

int main(int argc, char **argv) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  ::testing::InitGoogleMock(&argc, argv);
  curl_global_cleanup();
  std::time_t seed = time(NULL);
  printf("Tests using random seed of %lu\n", seed);
  srand(seed);
  return RUN_ALL_TESTS();
}
