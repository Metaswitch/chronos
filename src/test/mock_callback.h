#ifndef MOCK_CALLBACK_H__
#define MOCK_CALLBACK_H__

#include "callback.h"

#include "gmock/gmock.h"

class MockCallback : public Callback
{
public:
  MOCK_METHOD0(protocol, std::string());
  MOCK_METHOD1(perform, void(Timer*));
};

#endif
