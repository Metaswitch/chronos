#include "http_callback.h"
#include "base64.h"

#include <cstring>

HTTPCallback::HTTPCallback()
{
  _curl = curl_easy_init();

  curl_easy_setopt(_curl, CURLOPT_POST, 1);
  curl_easy_setopt(_curl, CURLOPT_READFUNCTION, string_write);
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
  curl_easy_setopt(_curl, CURLOPT_READDATA, &decoded_body);
  
  // Include the sequence number header.
  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, (std::string("X-Sequence-Number: ") +
                                        std::to_string(sequence_number)).c_str());
  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, headers);

  CURLcode curl_rc = curl_easy_perform(_curl);
  curl_slist_free_all(headers);
  if (curl_rc == CURLE_OK)
  {
    // Successful callback
    return true;
  }
  else
  {
    return false;
  }
}

// cURL utility callback for using a std::stream as the body for a request.
//
// WARNING this callback modifies `stream`.
size_t HTTPCallback::string_write(void* ptr, size_t size, size_t nmemb, void* stream)
{
  std::string* str = (std::string*)stream;

  // Must return 0 when we're done.
  if (str->empty())
  {
    return 0;
  }

  // Otherwise copy in an much as possible and return the length compied.
  strncpy((char *)ptr,
          str->c_str(),
          size * nmemb);
  if (str->length() < size *nmemb)
  {
    return str->length();
  }
  else
  {
    // Didn't copy the whole string, trim the bit we did write off the front.
    *str = str->substr(0, size * nmemb);
    return size * nmemb;
  }
}
