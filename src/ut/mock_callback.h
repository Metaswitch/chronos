/**
 * @file mock_callback.h
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

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
