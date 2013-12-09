#include "http_callback.h"
#include "base64.h"

#include <cstring>

HTTPCallback::HTTPCallback()
{
  _curl = curl_easy_init();

  curl_easy_setopt(_curl, CURLOPT_POST, 1);
  curl_easy_setopt(_curl, CURLOPT_VERBOSE, 1);
}

HTTPCallback::~HTTPCallback()
{
  curl_easy_cleanup(_curl);
}

// Perform the callback by sending the supplied body to the callback URL.
//
// Also specify the sequence number in the headers to allow duplicate detection/handling.
bool HTTPCallback::perform(std::string url, std::string encoded_body, unsigned int sequence_number)
{
  std::string decoded_body = base64_decode(encoded_body);
  curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, decoded_body.c_str());
  
  // Include the sequence number header.
  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, (std::string("X-Sequence-Number: ") +
                                        std::to_string(sequence_number)).c_str());
  headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, headers);

  CURLcode curl_rc = curl_easy_perform(_curl);
  curl_slist_free_all(headers);
  return (curl_rc == CURLE_OK);
}
