#pragma once
 
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>
 
// ─────────────────────────────────────────────
//  EventBus
//
//  Centralised pub/sub dispatcher for gameplay
//  events — BlockBreakEvent, BlockPlaceEvent, and
//  whatever comes next (entity damage, chunk load,
//  achievements, ...). Every system that *causes*
//  something (input handling, world edits) just
//  Publish()es an event; every system that *reacts*
//  to it (logging, audio, particles, stats, future
//  mod hooks) Subscribe()s independently. Neither
//  side needs to know the other exists.
//
//  Adding a new event type touches NOTHING in this
//  file — define a new plain struct (see
//  game/events/GameEvents.hpp) and Subscribe/Publish
//  against it. That's also what keeps this friendly
//  to future modding: a mod only needs to know about
//  the handful of event types it cares about, never
//  about every other system listening on the bus.
//
//  Usage:
//
//      EventBus::SubscriptionID id =
//          EventBus::Get().Subscribe<BlockBreakEvent>(
//              [](const BlockBreakEvent& e) { ... });
//
//      EventBus::Get().Publish(BlockBreakEvent{ pos, type });
//
//      EventBus::Get().Unsubscribe<BlockBreakEvent>(id);
//
//  Lifetime warning: this is a singleton with static
//  storage duration, so it outlives any object whose
//  destructor doesn't run until after main() returns.
//  If a listener lambda captures `this` (or any pointer
//  owned by Application/ChunkManager/etc.), Unsubscribe()
//  it during that owner's Shutdown()/destructor — exactly
//  like Application does for its own listeners.
//
//  Threading: Publish() dispatches synchronously, on
//  the calling thread, immediately. Subscribe/Publish/
//  Unsubscribe are NOT thread-safe with each other —
//  fine as long as the engine stays single-threaded
//  (true today). The moment a worker thread (chunk
//  generation, etc.) needs to raise an event, use
//  PublishDeferred() from that thread and call
//  FlushDeferred() once per tick from the main loop
//  instead — that path IS thread-safe and guarantees
//  every handler still runs on the main thread.
// ─────────────────────────────────────────────

class EventBus {
public:
    using SubscriptionID = uint64_t;

    static EventBus& Get() {
        static EventBus instance;
        return instance;
    }

    // Registers `handler` to be called every time an EventType is
    // Publish()ed. Returns an ID — keep it if you'll ever need to
    // Unsubscribe (you almost always will, see the lifetime warning
    // above).
    template <typename EventType>
    SubscriptionID Subscribe(std::function<void(const EventType&)> handler) {
        auto& bucket = m_handlers[std::type_index(typeid(EventType))];
        const SubscriptionID id = m_nextID++;
 
        bucket.push_back(Entry{
            id,
            [handler = std::move(handler)](const void* event) {
                handler(*static_cast<const EventType*>(event));
            }
        });

        return id;
    }

    // Removes a single listener previously returned by Subscribe<EventType>.
    // EventType must match the call that produced `id`.
    template <typename EventType>
    void Unsubscribe(SubscriptionID id) {
        auto it = m_handlers.find(std::type_index(typeid(EventType)));
        if (it == m_handlers.end()) return;

        auto& bucket = it->second;
        bucket.erase(
            std::remove_if(bucket.begin(), bucket.end(),
                [id](const Entry& entry) { return entry.id == id; }),
            bucket.end());
    }

    // Calls every listener subscribed to EventType, in subscription
    // order, synchronously, on the calling thread.
    template <typename EventType>
    void Publish(const EventType& event) const {
        auto it = m_handlers.find(std::type_index(typeid(EventType)));
        if (it == m_handlers.end()) return;

        for (const auto& entry : it->second) {
            entry.invoke(&event);
        }
    }

    // Thread-safe alternative to Publish() for callers that are NOT
    // the main thread. Queues `event`; nothing runs until the main
    // thread calls FlushDeferred().
    template <typename EventType>
    void PublishDeferred(EventType event) {
        std::lock_guard<std::mutex> lock(m_deferredMutex);
        m_deferred.push_back([this, event = std::move(event)]() {
            Publish(event);
        });
    }

    // Call once per tick from the main loop (e.g. the top of
    // Application::Update()). Runs every event queued since the
    // last flush, synchronously, on whichever thread calls this.
    void FlushDeferred() {
        std::vector<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(m_deferredMutex);
            pending.swap(m_deferred);
        }
        for (auto& fn : pending) fn();
    }

private:
    EventBus() = default;
 
    struct Entry {
        SubscriptionID id;
        std::function<void(const void*)> invoke;
    };
 
    std::unordered_map<std::type_index, std::vector<Entry>> m_handlers;
    SubscriptionID m_nextID = 1;
 
    std::vector<std::function<void()>> m_deferred;
    std::mutex m_deferredMutex;
};