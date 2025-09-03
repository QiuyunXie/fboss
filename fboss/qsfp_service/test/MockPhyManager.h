// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/lib/phy/PhyManager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

/*
 * MockPhyManager is a simple test class that inherits from PhyManager.
 * It provides GMOCK mocks for virtual methods, particularly programOnePort,
 * to enable testing of components that depend on PhyManager functionality.
 */
class MockPhyManager : public PhyManager {
 public:
  explicit MockPhyManager(const PlatformMapping* platformMapping)
      : PhyManager(platformMapping) {}

  // Mock the programOnePort method that was requested
  MOCK_METHOD4(
      programOnePort,
      void(PortID, cfg::PortProfileID, std::optional<TransceiverInfo>, bool));

  // Mock the pure virtual methods from PhyManager
  MOCK_CONST_METHOD1(getPhyIDInfo, phy::PhyIDInfo(GlobalXphyID));
  MOCK_CONST_METHOD1(getGlobalXphyID, GlobalXphyID(const phy::PhyIDInfo&));
  MOCK_METHOD1(initExternalPhyMap, bool(bool));
  MOCK_METHOD2(initializeSlotPhys, void(PimID, bool));
  MOCK_METHOD0(getSystemContainer, MultiPimPlatformSystemContainer*());
  MOCK_METHOD0(getPimStartNum, int());

 private:
  // Mock the pure virtual private methods
  MOCK_METHOD2(
      createExternalPhy,
      void(const phy::PhyIDInfo&, MultiPimPlatformPimContainer*));
  MOCK_METHOD1(
      createExternalPhyPortStats,
      std::unique_ptr<ExternalPhyPortStatsUtils>(PortID));
};

} // namespace facebook::fboss
