// Unit tests for eqt::state::EventBus (src/client/state/event_bus.cpp).
//
// Territory: tests/ only (T02, refactor/T02_state_event_bus.md). No source
// changes were made to produce this file.

#include "client/state/event_bus.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace eqt::state;

namespace {

GameEvent makePlayerMoved(float x = 1.0f) {
    PlayerMovedData data{};
    data.x = x;
    data.y = 2.0f;
    data.z = 3.0f;
    data.heading = 0.0f;
    data.dx = data.dy = data.dz = 0.0f;
    data.isMoving = true;
    return GameEvent(GameEventType::PlayerMoved, data);
}

GameEvent makeZoneChanged() {
    ZoneChangedData data{};
    data.zoneName = "qeynos";
    data.zoneId = 1;
    return GameEvent(GameEventType::ZoneChanged, data);
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Unfiltered subscribe receives every published event type.
// ---------------------------------------------------------------------------
TEST(EventBusTest, UnfilteredSubscribeReceivesAllTypes) {
    EventBus bus;
    std::vector<GameEventType> seen;
    bus.subscribe([&](const GameEvent& e) { seen.push_back(e.type); });

    bus.publish(makePlayerMoved());
    bus.publish(makeZoneChanged());

    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], GameEventType::PlayerMoved);
    EXPECT_EQ(seen[1], GameEventType::ZoneChanged);
}

// ---------------------------------------------------------------------------
// 2. Filtered subscribe receives only matching events.
// ---------------------------------------------------------------------------
TEST(EventBusTest, FilteredSubscribeReceivesOnlyMatchingType) {
    EventBus bus;
    int playerMovedCount = 0;
    int otherCount = 0;

    bus.subscribe(GameEventType::PlayerMoved, [&](const GameEvent&) { ++playerMovedCount; });
    bus.subscribe(GameEventType::ZoneChanged, [&](const GameEvent&) { ++otherCount; });

    bus.publish(makePlayerMoved());
    bus.publish(makePlayerMoved());
    bus.publish(makeZoneChanged());

    EXPECT_EQ(playerMovedCount, 2);
    EXPECT_EQ(otherCount, 1);
}

// ---------------------------------------------------------------------------
// 3. unsubscribe stops delivery; unsubscribing an already-removed or
//    never-issued handle is a no-op, not a crash.
// ---------------------------------------------------------------------------
TEST(EventBusTest, UnsubscribeStopsDelivery) {
    EventBus bus;
    int count = 0;
    ListenerHandle handle = bus.subscribe([&](const GameEvent&) { ++count; });

    bus.publish(makePlayerMoved());
    EXPECT_EQ(count, 1);

    bus.unsubscribe(handle);
    bus.publish(makePlayerMoved());
    EXPECT_EQ(count, 1); // no further delivery
}

TEST(EventBusTest, UnsubscribeAlreadyRemovedHandleIsNoOp) {
    EventBus bus;
    ListenerHandle handle = bus.subscribe([](const GameEvent&) {});
    bus.unsubscribe(handle);
    // Second unsubscribe of the same (now-removed) handle must not crash.
    EXPECT_NO_THROW(bus.unsubscribe(handle));
}

TEST(EventBusTest, UnsubscribeNeverIssuedHandleIsNoOp) {
    EventBus bus;
    // Handles start at 1 (see EventBus::m_nextHandle); 999999 was never issued.
    EXPECT_NO_THROW(bus.unsubscribe(static_cast<ListenerHandle>(999999)));
}

// ---------------------------------------------------------------------------
// 4. clear() removes all listeners.
// ---------------------------------------------------------------------------
TEST(EventBusTest, ClearRemovesAllListeners) {
    EventBus bus;
    int countA = 0, countB = 0;
    bus.subscribe([&](const GameEvent&) { ++countA; });
    bus.subscribe(GameEventType::PlayerMoved, [&](const GameEvent&) { ++countB; });

    bus.clear();
    bus.publish(makePlayerMoved());

    EXPECT_EQ(countA, 0);
    EXPECT_EQ(countB, 0);
}

// ---------------------------------------------------------------------------
// 5. Multiple listeners on the same type all fire; handles are distinct.
// ---------------------------------------------------------------------------
TEST(EventBusTest, MultipleListenersOnSameTypeAllFire) {
    EventBus bus;
    int a = 0, b = 0, c = 0;
    ListenerHandle h1 = bus.subscribe(GameEventType::PlayerMoved, [&](const GameEvent&) { ++a; });
    ListenerHandle h2 = bus.subscribe(GameEventType::PlayerMoved, [&](const GameEvent&) { ++b; });
    ListenerHandle h3 = bus.subscribe(GameEventType::PlayerMoved, [&](const GameEvent&) { ++c; });

    EXPECT_NE(h1, h2);
    EXPECT_NE(h2, h3);
    EXPECT_NE(h1, h3);

    bus.publish(makePlayerMoved());

    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(c, 1);
}

// ---------------------------------------------------------------------------
// 6. Templated publish(type, data) delivers the same event as publish(GameEvent&).
// ---------------------------------------------------------------------------
TEST(EventBusTest, TemplatedPublishConvenienceOverloadDeliversSameEvent) {
    EventBus bus;
    GameEventType receivedType{};
    float receivedX = 0.0f;

    bus.subscribe([&](const GameEvent& e) {
        receivedType = e.type;
        receivedX = std::get<PlayerMovedData>(e.data).x;
    });

    PlayerMovedData data{};
    data.x = 42.0f;
    bus.publish(GameEventType::PlayerMoved, data);

    EXPECT_EQ(receivedType, GameEventType::PlayerMoved);
    EXPECT_FLOAT_EQ(receivedX, 42.0f);
}

// ---------------------------------------------------------------------------
// Re-entrancy: publish() copies the listener vector under the mutex, then
// invokes the copies with the lock released. Two consequences fall out of
// that, and both are pinned here as the actual, current behavior.
// ---------------------------------------------------------------------------

// A listener that unsubscribes ANOTHER listener during dispatch does NOT
// prevent that other listener from being called for the in-flight event --
// it was already copied into the dispatch list before the unsubscribe ran.
TEST(EventBusTest, ReentrantUnsubscribeDuringDispatchStillDeliversToVictim) {
    EventBus bus;
    bool victimCalled = false;
    ListenerHandle victimHandle{};

    victimHandle = bus.subscribe([&](const GameEvent&) { victimCalled = true; });
    bus.subscribe([&](const GameEvent&) {
        // Unsubscribe the victim mid-dispatch. Per the plan's documented
        // behavior, this must NOT stop the victim from firing for the
        // event currently being published (listeners were already copied).
        bus.unsubscribe(victimHandle);
    });

    bus.publish(makePlayerMoved());

    EXPECT_TRUE(victimCalled) << "listener copied before dispatch must still "
                                  "fire even if unsubscribed mid-dispatch";

    // Confirm the unsubscribe DID take effect for subsequent publishes.
    victimCalled = false;
    bus.publish(makePlayerMoved());
    EXPECT_FALSE(victimCalled);
}

// A listener that subscribes a NEW listener during dispatch does NOT have
// that new listener invoked for the in-flight event (it wasn't in the
// copied dispatch list), but IS invoked for subsequent publishes.
TEST(EventBusTest, ReentrantSubscribeDuringDispatchNotInvokedForInFlightEvent) {
    EventBus bus;
    bool newListenerCalledDuringFirstPublish = false;
    int newListenerCallCount = 0;

    bus.subscribe([&](const GameEvent&) {
        bus.subscribe([&](const GameEvent&) {
            newListenerCalledDuringFirstPublish = true;
            ++newListenerCallCount;
        });
    });

    bus.publish(makePlayerMoved());
    EXPECT_FALSE(newListenerCalledDuringFirstPublish)
        << "listener added during dispatch must not fire for the event "
           "already in flight";
    EXPECT_EQ(newListenerCallCount, 0);

    // The newly-subscribed listener DOES fire on the next publish.
    bus.publish(makePlayerMoved());
    EXPECT_EQ(newListenerCallCount, 1);
}

// ---------------------------------------------------------------------------
// 7. Thread safety: concurrent publish() from N threads with a listener
//    incrementing an atomic counter delivers exactly the expected total.
//    Fixed thread count, fixed iteration count, join all threads, assert an
//    exact number. No sleeps, no timing assertions.
// ---------------------------------------------------------------------------
TEST(EventBusTest, ConcurrentPublishDeliversExactCount) {
    EventBus bus;
    std::atomic<int> counter{0};
    bus.subscribe([&](const GameEvent&) { counter.fetch_add(1, std::memory_order_relaxed); });

    constexpr int kThreads = 8;
    constexpr int kIterationsPerThread = 500;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&bus]() {
            for (int i = 0; i < kIterationsPerThread; ++i) {
                bus.publish(makePlayerMoved());
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(counter.load(), kThreads * kIterationsPerThread);
}

// Note (not tested, per the plan): listener lifetime -- a listener capturing
// `this` of an object destroyed mid-dispatch is a genuine dangling-reference
// hazard in the current EventBus design (nothing keeps the listened-to
// object alive across the copy-then-invoke window). It is UB and cannot be
// exercised safely in a unit test; flagged here for the tracker instead of
// asserted.
