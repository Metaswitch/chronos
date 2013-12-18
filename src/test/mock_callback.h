#ifndef MOCK_CALLBACK_H__
#define MOCK_CALLBACK_H__

#include "callback.h"

#include "gmock/gmock.h"

class MockCallback : public Callback
{
public:
  MOCK_METHOD0(protocol, std::string());
  MOCK_METHOD3(perform, bool(std::string, std::string, unsigned int));
};

#endif
