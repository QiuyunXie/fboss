/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/BaseEcmpResourceManagerTest.h"

namespace facebook::fboss {

class EcmpBackupGroupTypeTest : public BaseEcmpResourceManagerTest {
 public:
  std::shared_ptr<EcmpResourceManager> makeResourceMgr() const override {
    static constexpr auto kEcmpGroupHwLimit = 7;
    return std::make_shared<EcmpResourceManager>(
        kEcmpGroupHwLimit,
        0 /*compressionPenaltyThresholdPct*/,
        cfg::SwitchingMode::PER_PACKET_RANDOM);
  }
  static constexpr auto kNumStartRoutes = 5;
  int numStartRoutes() const override {
    return kNumStartRoutes;
  }
  std::vector<RouteNextHopSet> defaultNhopSets() const {
    std::vector<RouteNextHopSet> defaultNhopGroups;
    auto beginNhops = defaultNhops();
    for (auto i = 0; i < numStartRoutes(); ++i) {
      defaultNhopGroups.push_back(beginNhops);
      beginNhops.erase(beginNhops.begin());
      CHECK_GT(beginNhops.size(), 1);
    }
    return defaultNhopGroups;
  }

  std::vector<RouteNextHopSet> nextNhopSets(int numSets = kNumStartRoutes) {
    auto nhopsStart = defaultNhopSets().back();
    std::vector<RouteNextHopSet> nhopsTo;
    for (auto i = 0; i < numSets; ++i) {
      nhopsStart.erase(nhopsStart.begin());
      CHECK_GT(nhopsStart.size(), 1);
      nhopsTo.push_back(nhopsStart);
    }
    return nhopsTo;
  }

  void SetUp() override {
    BaseEcmpResourceManagerTest::SetUp();
    XLOG(DBG2) << "EcmpResourceMgrBackupGrpTest SetUp";
    CHECK(state_->isPublished());
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto newNhops = defaultNhopSets();
    CHECK_EQ(fib6->size(), newNhops.size());
    auto idx = 0;
    for (auto [_, route] : *fib6) {
      auto newRoute = route->clone();
      newRoute->setResolved(
          RouteNextHopEntry(newNhops[idx++], kDefaultAdminDistance));
      newRoute->publish();
      fib6->updateNode(newRoute);
    }
    newState->publish();
    consolidate(newState);
    assertEndState(newState, {});
    XLOG(DBG2) << "EcmpResourceMgrBackupGrpTest SetUp done";
  }
  void assertEndState(
      const std::shared_ptr<SwitchState>& endStatePrefixes,
      const std::set<RouteV6::Prefix>& overflowPrefixes) {
    EXPECT_EQ(state_->getFibs()->getNode(RouterID(0))->getFibV4()->size(), 0);
    for (auto [_, inRoute] : std::as_const(*cfib(endStatePrefixes))) {
      auto route = cfib(state_)->exactMatch(inRoute->prefix());
      ASSERT_TRUE(route->isResolved());
      ASSERT_NE(route, nullptr);
      if (overflowPrefixes.find(route->prefix()) != overflowPrefixes.end()) {
        EXPECT_TRUE(
            route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value())
            << " expected route " << route->str()
            << " to have override ECMP group type";
        EXPECT_EQ(
            route->getForwardInfo().getOverrideEcmpSwitchingMode(),
            consolidator_->getBackupEcmpSwitchingMode());
      } else {
        EXPECT_FALSE(
            route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value());
      }
    }
  }
};

TEST_F(EcmpBackupGroupTypeTest, addSingleNhopRoutesBelowEcmpLimit) {
  // add new routes pointing to single nhops, no limit is thus breached
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto nhopSet = defaultNhops();
  auto nhopItr = nhopSet.begin();
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route =
        makeRoute(makePrefix(routesBefore + i), RouteNextHopSet{*nhopItr});
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, addRemoveSingleNhopRoutesBelowEcmpLimit) {
  std::set<RouteV6::Prefix> singleNhopPrefixes;
  // add new routes pointing to single nhops, no limit is thus breached
  {
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.begin();
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto route =
          makeRoute(makePrefix(routesBefore + i), RouteNextHopSet{*nhopItr});
      singleNhopPrefixes.insert(route->prefix());
      fib6->addNode(route);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  {
    // Remove single nhop routes.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    for (const auto& pfx : singleNhopPrefixes) {
      auto route = fib6->getRouteIf(pfx);
      fib6->removeNode(route);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}
// Convert routes to single nhop and back to mnhops. No spillover before
// and after
TEST_F(EcmpBackupGroupTypeTest, updateRouteBelowEcmpLimitToSingleNhop) {
  std::set<RouteV6::Prefix> overflowPrefixes;
  {
    // Update all routes to a single NHOP. No limit is breached before
    // or after.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.begin();
    for (const auto& [_, route] : std::as_const(*cfib(oldState))) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Update all routes back to mnhops. No limit breached before or after.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = defaultNhopSets();
    auto nhopSetItr = nhopSets.begin();
    for (const auto& [_, route] : std::as_const(*cfib(oldState))) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(
          RouteNextHopEntry(*nhopSetItr, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateRoutesSingleNhopToSingleNhop) {
  std::set<RouteV6::Prefix> overflowPrefixes;
  {
    // Update all routes to a single NHOP. No limit is breached before
    // or after.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.begin();
    for (const auto& [_, route] : std::as_const(*cfib(oldState))) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Update all routes to different single nhops, no limit is breached
    // before or after
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.rbegin();
    for (const auto& [_, route] : std::as_const(*cfib(oldState))) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
}
// Convert spillover routes to single nhop. Now no spillover.
TEST_F(EcmpBackupGroupTypeTest, updateRouteAboveEcmpLimitToSingleNhop) {
  std::set<RouteV6::Prefix> addedPrefixes;
  {
    // Update a route pointing to new nhops. ECMP limit is breached during
    // update due to make before break. Then the reclaim step notices
    // a freed up primary ECMP group and reclaims it back. So in the
    // end no prefixes have back up ecmp group override set.
    auto nhopSets = nextNhopSets();
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto newRoute = fib6->cbegin()->second->clone();
    newRoute->setResolved(
        RouteNextHopEntry(*nhopSets.begin(), kDefaultAdminDistance));
    addedPrefixes.insert(newRoute->prefix());
    fib6->updateNode(newRoute);
    auto deltas = consolidate(newState);
    // Base FIB delta + overflow delta + reclaim delta
    EXPECT_EQ(deltas.size(), 3);
    assertEndState(newState, {});
  }
  {
    // Update the overflow route to single nhop. Should no longer
    // be of backupGroupType, since single nhop groups don't contribute
    // to ecmp groups
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto newRoute = fib6->getRouteIf(*addedPrefixes.begin())->clone();
    newRoute->setResolved(RouteNextHopEntry(
        RouteNextHopSet{*nhopSet.begin()}, kDefaultAdminDistance));
    fib6->updateNode(newRoute);
    auto deltas = consolidate(newState);
    // Single delta, no spillover expected
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, addRoutesBelowEcmpLimit) {
  // add new routes pointing to existing nhops. No limit is thus breached.
  auto nhopSets = defaultNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, addRoutesAboveEcmpLimit) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), numStartRoutes() + 1);
  assertEndState(newState, overflowPrefixes);
}

TEST_F(EcmpBackupGroupTypeTest, addRoutesAboveEcmpLimitAndReplay) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), numStartRoutes() + 1);
  assertEndState(newState, overflowPrefixes);
  {
    // Replay state with new pointers for the overflow routes,
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned overflow routes, "
               << "should get identical result";
    auto newerState = state_->clone();
    fib6 = fib(newerState);
    for (const auto& pfx : overflowPrefixes) {
      auto route = fib6->getRouteIf(pfx)->clone();
      route->publish();
      fib6->updateNode(route);
    }
    auto deltas2 = consolidate(newState);
    EXPECT_EQ(deltas2.size(), overflowPrefixes.size() + 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Replay state with new pointers for non overflow routes,
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned non-overflow routes, "
               << "should get identical result";
    auto newerState = state_->clone();
    fib6 = fib(newerState);
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto route = fib6->getRouteIf(makePrefix(i))->clone();
      route->publish();
      fib6->updateNode(route);
    }
    auto deltas2 = consolidate(newerState);
    EXPECT_EQ(deltas2.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Replay state with new pointers for all routes
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned non-overflow routes, "
               << "should get identical result";
    auto newerState = state_->clone();
    fib6 = fib(newerState);
    for (const auto& [_, origRoute] : std::as_const(*cfib(state_))) {
      auto route = fib6->getRouteIf(origRoute->prefix())->clone();
      route->publish();
      fib6->updateNode(route);
    }
    auto deltas2 = consolidate(newerState);
    EXPECT_EQ(deltas2.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateRoutesToSingleNhopGroups) {
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto nhopSet = defaultNhops();
  auto nhopItr = nhopSet.begin();
  for (const auto& [_, route] : std::as_const(*cfib(oldState))) {
    auto newRoute = fib6->getRouteIf(route->prefix())->clone();
    newRoute->setResolved(
        RouteNextHopEntry(RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
    fib6->updateNode(newRoute);
  }
  auto deltas = consolidate(newState);
  // Single delta, no spillover expected
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, updateRouteBelowEcmpLimit) {
  // Update a route pointing to new nhops. But stay below ECMP limit
  {
    // Remove one route so we go below ECMP limit
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto rmRoute = fib6->cbegin()->second->clone();
    fib6->removeNode(rmRoute);
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  {
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto newRoute = fib6->cbegin()->second->clone();
    newRoute->setResolved(
        RouteNextHopEntry(*nextNhopSets().begin(), kDefaultAdminDistance));
    fib6->updateNode(newRoute);
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateRouteAboveEcmpLimit) {
  // Update a route pointing to new nhops. ECMP limit is breached during
  // update due to make before break. Then the reclaim step notices
  // a freed up primary ECMP group and reclaims it back. So in the
  // end no prefixes have back up ecmp group override set.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto newRoute = fib6->cbegin()->second->clone();
  std::set<RouteV6::Prefix> overflowPrefixes;
  newRoute->setResolved(
      RouteNextHopEntry(*nhopSets.begin(), kDefaultAdminDistance));
  fib6->updateNode(newRoute);
  auto deltas = consolidate(newState);
  // Initial update + overflow + reclaim
  EXPECT_EQ(deltas.size(), 3);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, updateAllRoutesOneRouteAboveEcmpLimit) {
  // Update a route pointing to new nhops. ECMP limit is breached.
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  std::set<RouteV6::Prefix> overflowPrefixes;
  auto nhopSets = nextNhopSets();
  ASSERT_EQ(nhopSets.size(), fib6->size());
  auto idx = 0;
  for (const auto& [_, route] : std::as_const(*cfib(oldState))) {
    auto newRoute = fib6->getRouteIf(route->prefix())->clone();
    newRoute->setResolved(
        RouteNextHopEntry(nhopSets[idx++], kDefaultAdminDistance));
    fib6->updateNode(newRoute);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 3);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, updateAllRoutesAllRoutesAboveEcmpLimit) {
  std::set<RouteV6::Prefix> startPrefixes;
  for (const auto& [_, route] : std::as_const(*cfib(state_))) {
    startPrefixes.insert(route->prefix());
  }
  {
    // Add routes pointing to existing nhops. ECMP limit is not breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto existingNhopSets = defaultNhopSets();
    auto routesBefore = fib6->size();
    for (auto i = 0; i < routesBefore; ++i) {
      auto newRoute =
          makeRoute(makePrefix(routesBefore + i), existingNhopSets[i]);
      fib6->addNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  /*
   * Routes before
   * R1, R5 - G1
   * R2, R6 - G2
   * R3, R7 - G3
   * R4, R8 - G4
   * R0, R9 - G5
   * Routes after
   * R1 - G6
   * R2 - G7
   * R3 - G8
   * R4 - G9
   * R5 - G1
   * R6 - G2
   * R7 - G3
   * R8 - G4
   * R9 - G5
   * R0 - G10
   *
   */
  {
    // Update 5 routes pointing to new nhops. ECMP limit is breached as
    // 2 routes pointing to new nhops get updated one by one. Later when
    // both routes get updated, one ecmp group gets removed. Later reclaim
    // step comes a moves all groups back to primary ecmp group type
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    std::set<RouteV6::Prefix> overflowPrefixes;
    auto nhopSets = nextNhopSets();
    auto idx = 0;
    for (const auto& prefix : startPrefixes) {
      auto newRoute = fib6->getRouteIf(prefix)->clone();
      newRoute->setResolved(
          RouteNextHopEntry(nhopSets[idx++], kDefaultAdminDistance));
      overflowPrefixes.insert(newRoute->prefix());
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    // Initial delta + 5 overflow deltas, no reclaima
    EXPECT_EQ(deltas.size(), 6);
    assertEndState(newState, overflowPrefixes);
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateAllRoutesToNewNhopsAboveEcmpLimit) {
  {
    // Add routes pointing to existing nhops. ECMP limit is not breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto existingNhopSets = defaultNhopSets();
    auto routesBefore = fib6->size();
    for (auto i = 0; i < routesBefore; ++i) {
      auto newRoute =
          makeRoute(makePrefix(routesBefore + i), existingNhopSets[i]);
      fib6->addNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  {
    /*
     * Initial state
     * R1, R5 -> G1
     * R2, R6 -> G2
     * R3, R7 -> G3
     * R4, R8 -> G4
     * R0, R9 -> G5
     * New state
     * R1 -> G6
     * R2 -> G7
     * R3 -> G8
     * R4 -> G9
     * R5 -> G10
     * R6 -> G10
     * R7 -> G9
     * R8 -> G8
     * R9 -> G7
     * R0 -> G6
     */
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = nextNhopSets();
    auto idx = 0;
    bool idxDirectionFwd = true;
    for (const auto& [_, route] : std::as_const(*cfib(oldState))) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          nhopSets[idxDirectionFwd ? idx++ : idx--], kDefaultAdminDistance));
      fib6->updateNode(newRoute);
      if (idx == nhopSets.size()) {
        idxDirectionFwd = false;
        --idx;
      }
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 7);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, reclaimPrioritizesECMPWithMoreRoutes) {
  std::set<RouteV6::Prefix> overflowPrefixes;
  size_t routesStart = cfib(state_)->size();
  {
    /*
     * Initial state
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G5
     * New state
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G5
     * R5 -> G6 (backup group type)
     * R6 -> G7 (backup group type)
     */
    // Add 2 routes pointing to new nhops. ECMP limit is breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = nextNhopSets();
    ASSERT_EQ(nhopSets.size(), fib6->size());
    for (auto i = 0; i < 2; ++i) {
      auto route = makeRoute(makePrefix(routesStart + i), nhopSets[i]);
      overflowPrefixes.insert(route->prefix());
      fib6->addNode(route);
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(
          RouteNextHopEntry(nhopSets[i], kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 3);
    assertEndState(newState, overflowPrefixes);
  }
  {
    /*
     * Old state
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G5
     * R5 -> G6 (backup group type)
     * R6 -> G7 (backup group type)
     * New State
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G7
     * R5 -> G6 (backup group type)
     * R6 -> G7
     * Notice that the group with 2 routes pointing to it is prioritized for
     * reclaim (over the group with one route pointing to it)
     */
    // Add 2 routes pointing to new nhops. ECMP limit is breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = nextNhopSets();
    auto prefixFrom = makePrefix(fib6->size() - 1);
    auto nhopsFrom =
        fib6->getRouteIf(prefixFrom)->getForwardInfo().getNextHopSet();
    auto updateRoute = fib6->getRouteIf(makePrefix(0))->clone();
    updateRoute->setResolved(
        RouteNextHopEntry(nhopsFrom, kDefaultAdminDistance));
    fib6->updateNode(updateRoute);
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 2);
    // prefixFrom should now longer point to backupGroupType since it
    // will be reclaimed as one ECMP group gets freed due to update
    // of R0
    overflowPrefixes.erase(prefixFrom);
    assertEndState(newState, overflowPrefixes);
  }
}

} // namespace facebook::fboss
