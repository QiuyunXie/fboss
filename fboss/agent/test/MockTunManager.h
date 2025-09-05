/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <gmock/gmock.h>
#include <optional>

#include "fboss/agent/TunManager.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

class RxPacket;

class MockTunManager : public TunManager {
 public:
  MockTunManager(SwSwitch* sw, FbossEventBase* evb);
  ~MockTunManager() override {}

  MOCK_METHOD0(startObservingUpdates, void());
  MOCK_METHOD1(stateUpdated, void(const StateDelta& delta));
  MOCK_METHOD1(sync, void(std::shared_ptr<SwitchState>));

  MOCK_METHOD1(
      sendPacketToHost_,
      bool(std::tuple<InterfaceID, std::shared_ptr<RxPacket>>));
  bool sendPacketToHost(InterfaceID, std::unique_ptr<RxPacket> pkt) override;
  MOCK_METHOD1(doProbe, void(std::lock_guard<std::mutex>&));
  MOCK_METHOD4(
      addRemoveRouteTable,
      void(
          int tableId,
          int ifIndex,
          bool add,
          std::optional<InterfaceID> ifID));
  MOCK_METHOD4(
      addRemoveSourceRouteRule,
      void(
          int tableId,
          const folly::IPAddress& addr,
          bool add,
          std::optional<InterfaceID> ifID));
  MOCK_METHOD5(
      addRemoveTunAddress,
      void(
          const std::string& ifName,
          uint32_t ifIndex,
          const folly::IPAddress& addr,
          uint8_t mask,
          bool add));
};

} // namespace facebook::fboss
