#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <curl/curl.h>

int main(int argc, char **argv) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  ::testing::InitGoogleMock(&argc, argv);
  curl_global_cleanup();
  return RUN_ALL_TESTS();
}
