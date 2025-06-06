// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
//
#include "fboss/agent/FabricConnectivityManager.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/test/agent_hw_tests/AgentVoqSwitchFullScaleDsfTests.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/VoqTestUtils.h"

namespace facebook::fboss {

class AgentVoqSwitchScaleTest : public AgentVoqSwitchFullScaleDsfNodesTest {};

TEST_F(AgentVoqSwitchScaleTest, remoteNeighborWithEcmpGroup) {
  const auto kEcmpWidth = getMaxEcmpWidth();
  const auto kMaxDeviation = 25;
  auto setup = [&]() {
    utility::setupRemoteIntfAndSysPorts(
        getSw(),
        isSupportedOnAllAsics(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE));
    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());

    // Resolve remote nhops and get a list of remote sysPort descriptors
    flat_set<PortDescriptor> sysPortDescs =
        utility::resolveRemoteNhops(getAgentEnsemble(), ecmpHelper);

    CHECK(sysPortDescs.size() > kEcmpWidth);
    for (int i = 0; i < getMaxEcmpGroup() / 2; i++) {
      auto prefix = RoutePrefixV6{
          folly::IPAddressV6(folly::to<std::string>(i, "::", i)),
          static_cast<uint8_t>(i == 0 ? 0 : 128)};
      auto routeUpdater = getSw()->getRouteUpdater();
      auto sysPortStart = (i * kEcmpWidth) % sysPortDescs.size();
      auto ecmpMemberPorts = flat_set<PortDescriptor>(
          std::make_move_iterator(sysPortDescs.begin() + sysPortStart),
          std::make_move_iterator(
              sysPortDescs.begin() +
              std::min(sysPortStart + kEcmpWidth, sysPortDescs.size())));
      // Wrap around and start adding from front of the ports
      if (ecmpMemberPorts.size() < kEcmpWidth) {
        ecmpMemberPorts.insert(
            std::make_move_iterator(sysPortDescs.begin()),
            std::make_move_iterator(
                sysPortDescs.begin() + (kEcmpWidth - ecmpMemberPorts.size())));
      }
      ecmpHelper.programRoutes(&routeUpdater, ecmpMemberPorts, {prefix});
    }
  };
  auto verify = [&]() {
    auto sysPortDescs = getRemoteSysPortDesc();
    // Send and verify packets across voq drops.
    auto defaultRouteSysPorts = std::vector<PortDescriptor>(
        sysPortDescs.begin(), sysPortDescs.begin() + kEcmpWidth);
    std::function<std::map<SystemPortID, HwSysPortStats>(
        const std::vector<SystemPortID>&)>
        getSysPortStatsFn = [&](const std::vector<SystemPortID>& portIds) {
          getSw()->updateStats();
          return getLatestSysPortStats(portIds);
        };
    utility::pumpTrafficAndVerifyLoadBalanced(
        [&]() {
          utility::pumpTraffic(
              true, /* isV6 */
              utility::getAllocatePktFn(getAgentEnsemble()),
              utility::getSendPktFunc(getAgentEnsemble()),
              utility::kLocalCpuMac(), /* dstMac */
              std::nullopt, /* vlan */
              std::nullopt, /* frontPanelPortToLoopTraffic */
              255, /* hopLimit */
              1000000 /* numPackets */);
        },
        [&]() {
          auto ports = std::make_unique<std::vector<int32_t>>();
          for (auto sysPortDecs : defaultRouteSysPorts) {
            ports->push_back(static_cast<int32_t>(sysPortDecs.sysPortID()));
          }
          getSw()->clearPortStats(ports);
        },
        [&]() {
          WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(utility::isLoadBalanced(
              defaultRouteSysPorts,
              {},
              getSysPortStatsFn,
              kMaxDeviation,
              false)));
          return true;
        });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchScaleTest, remoteAndLocalLoadBalance) {
  const auto kEcmpWidth = 16;
  const auto kMaxDeviation = 25;
  auto setup = [&]() {
    utility::setupRemoteIntfAndSysPorts(
        getSw(),
        isSupportedOnAllAsics(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE));
    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());

    // Resolve remote and local nhops and get a list of sysPort descriptors
    auto remoteSysPortDescs =
        utility::resolveRemoteNhops(getAgentEnsemble(), ecmpHelper);
    auto localSysPortDescs = resolveLocalNhops(ecmpHelper);
    std::vector<PortDescriptor> sysPortDescs;
    sysPortDescs.insert(
        sysPortDescs.end(),
        remoteSysPortDescs.begin(),
        remoteSysPortDescs.begin() + kEcmpWidth / 2);
    sysPortDescs.insert(
        sysPortDescs.end(),
        localSysPortDescs.begin(),
        localSysPortDescs.begin() + kEcmpWidth / 2);

    auto prefix = RoutePrefixV6{folly::IPAddressV6("0::0"), 0};
    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &routeUpdater,
        flat_set<PortDescriptor>(
            std::make_move_iterator(sysPortDescs.begin()),
            std::make_move_iterator(sysPortDescs.end())),
        {prefix});
  };
  auto verify = [&]() {
    std::vector<PortDescriptor> sysPortDescs;
    auto remoteSysPortDescs = getRemoteSysPortDesc();
    auto localSysPortDescs = getInterfacePortSysPortDesc();

    sysPortDescs.insert(
        sysPortDescs.end(),
        remoteSysPortDescs.begin(),
        remoteSysPortDescs.begin() + kEcmpWidth / 2);
    sysPortDescs.insert(
        sysPortDescs.end(),
        localSysPortDescs.begin(),
        localSysPortDescs.begin() + kEcmpWidth / 2);

    // Send and verify packets across voq drops.
    std::function<std::map<SystemPortID, HwSysPortStats>(
        const std::vector<SystemPortID>&)>
        getSysPortStatsFn = [&](const std::vector<SystemPortID>& portIds) {
          getSw()->updateStats();
          return getLatestSysPortStats(portIds);
        };
    utility::pumpTrafficAndVerifyLoadBalanced(
        [&]() {
          utility::pumpTraffic(
              true, /* isV6 */
              utility::getAllocatePktFn(getAgentEnsemble()),
              utility::getSendPktFunc(getAgentEnsemble()),
              utility::kLocalCpuMac(), /* dstMac */
              std::nullopt, /* vlan */
              std::nullopt, /* frontPanelPortToLoopTraffic */
              255, /* hopLimit */
              10000 /* numPackets */);
        },
        [&]() {
          auto ports = std::make_unique<std::vector<int32_t>>();
          for (auto sysPortDecs : sysPortDescs) {
            ports->push_back(static_cast<int32_t>(sysPortDecs.sysPortID()));
          }
          getSw()->clearPortStats(ports);
        },
        [&]() {
          WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(utility::isLoadBalanced(
              sysPortDescs, {}, getSysPortStatsFn, kMaxDeviation, false)));
          return true;
        });
  };
  verifyAcrossWarmBoots(setup, verify);
};

TEST_F(AgentVoqSwitchScaleTest, stressProgramEcmpRoutes) {
  auto kEcmpWidth = getMaxEcmpWidth();
  const auto routeScale = 5;
  const auto numIterations = 4;
  auto setup = [&]() {
    utility::setupRemoteIntfAndSysPorts(
        getSw(),
        isSupportedOnAllAsics(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE));
    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());

    // Resolve remote nhops and get a list of remote sysPort descriptors
    auto sysPortDescs =
        utility::resolveRemoteNhops(getAgentEnsemble(), ecmpHelper);

    for (int iter = 0; iter < numIterations; iter++) {
      std::vector<RoutePrefixV6> routes;
      for (int i = 0; i < routeScale; i++) {
        auto prefix = RoutePrefixV6{
            folly::IPAddressV6(folly::to<std::string>(i + 1, "::", i + 1)),
            128};
        auto routeUpdater = getSw()->getRouteUpdater();
        ecmpHelper.programRoutes(
            &routeUpdater,
            flat_set<PortDescriptor>(
                std::make_move_iterator(sysPortDescs.begin() + i),
                std::make_move_iterator(sysPortDescs.begin() + i + kEcmpWidth)),
            {prefix});
        routes.push_back(prefix);
      }
      auto routeUpdater = getSw()->getRouteUpdater();
      ecmpHelper.unprogramRoutes(&routeUpdater, routes);
    }
  };
  verifyAcrossWarmBoots(setup, [] {});
}

class AgentVoqSwitchScaleWithFabricPortsTest
    : public AgentVoqSwitchFullScaleDsfNodesTest {
 private:
  void setCmdLineFlagOverrides() const override {
    AgentVoqSwitchFullScaleDsfNodesTest::setCmdLineFlagOverrides();
    // Unhide fabric ports
    FLAGS_hide_fabric_ports = false;
    // Fabric connectivity manager to expect single NPU
    if (!FLAGS_multi_switch) {
      FLAGS_janga_single_npu_for_testing = true;
    }
  }
};

TEST_F(AgentVoqSwitchScaleWithFabricPortsTest, failUpdateAtFullSysPortScale) {
  auto setup = [this]() {
    utility::setupRemoteIntfAndSysPorts(
        getSw(),
        isSupportedOnAllAsics(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE));
  };
  auto verify = [this]() {
    getSw()->getRib()->updateStateInRibThread([this]() {
      EXPECT_THROW(
          getSw()->updateStateWithHwFailureProtection(
              "Add bogus intf",
              [this](const std::shared_ptr<SwitchState>& in) {
                auto out = in->clone();
                auto remoteIntfs = out->getRemoteInterfaces()->modify(&out);
                auto remoteIntf = std::make_shared<Interface>(
                    InterfaceID(INT32_MAX),
                    RouterID(0),
                    std::optional<VlanID>(std::nullopt),
                    folly::StringPiece("RemoteIntf"),
                    folly::MacAddress("c6:ca:2b:2a:b1:b6"),
                    9000,
                    false,
                    false,
                    cfg::InterfaceType::SYSTEM_PORT);
                remoteIntf->setScope(cfg::Scope::GLOBAL);
                remoteIntfs->addNode(
                    remoteIntf,
                    HwSwitchMatcher(
                        getSw()->getSwitchInfoTable().getL3SwitchIDs()));
                return out;
              }),
          FbossHwUpdateError);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
