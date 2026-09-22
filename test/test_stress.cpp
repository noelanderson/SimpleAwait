// Stress suite — V1 hardening.
//
// Each scenario drives a primitive hard, then drains to idle and asserts the
// required final invariants via the diagnostics snapshot: activeTasks == 0,
// frameBytesUsed == 0 (every coroutine frame recovered — no leak, no stale link),
// and allocationFailures == 0. A lifetime-counting payload additionally proves no
// double destroy across heavy Queue churn. Deterministic and allocation-free in
// the scheduler; the fake clock drives timers.

#include <cstddef>
#include <cstdint>

#define SIMPLEAWAIT_ENABLE_DIAGNOSTICS 1
namespace {
unsigned long long g_now = 0;
int g_last_error = -1;
}
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)
// Non-halting recording hook: the allocator-fragmentation scenario relies on a
// frame-pool exhaustion (raised only if a broken coalescing deallocate fails to
// recover a large frame) being observable as a clean assertion rather than an
// abort. No passing scenario ever triggers it, and drainAndCheckIdle asserts it
// stayed clear.
#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::delay_us;
using simpleawait::Event;
using simpleawait::poll;
using simpleawait::Queue;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Stats;
using simpleawait::stats;
using simpleawait::Task;
using simpleawait::ThreadSafeFlag;

namespace {

// Drain the scheduler to idle (bounded), then assert the final invariants.
void drainAndCheckIdle() {
    auto& sch = scheduler();
    int guard = 0;
    while (sch.activeTaskCount() > 0 && guard++ < 2000000) {
        poll();
    }
    const Stats s = stats();
    SA_CHECK(s.activeTasks == 0);
    SA_CHECK(s.frameBytesUsed == 0);
    SA_CHECK(s.allocationFailures == 0);
    SA_CHECK(g_last_error == -1);
}

int g_counter = 0;
Task<void> trivial() {
    ++g_counter;
    co_return;
}

Task<void> leaf() { co_return; }
Task<void> nest(int depth) {
    if (depth > 0) {
        co_await nest(depth - 1);
    } else {
        co_await leaf();
    }
}

template <size_t N>
Task<void> sized() {
    volatile char buf[N];
    buf[0] = static_cast<char>(N);
    (void)buf[0];
    co_await simpleawait::yield();
}

// A frame that stays parked (holding its pool block) until *ev* is set.
template <size_t N>
Task<void> holdSized(Event* ev) {
    volatile char buf[N];
    buf[0] = static_cast<char>(N);
    (void)buf[0];
    co_await ev->wait();
}

Task<void> napper(uint64_t us) {
    co_await delay_us(us);
}

Task<void> evWaiter(Event* ev, int* count) {
    co_await ev->wait();
    ++(*count);
}

Task<void> flagConsumer(ThreadSafeFlag* f, int iters, int* count) {
    for (int i = 0; i < iters; ++i) {
        co_await f->wait();
        ++(*count);
    }
}

Task<void> qProducer(Queue<int, 4>* q, int n) {
    for (int i = 0; i < n; ++i) {
        co_await q->send(i);
    }
}
Task<void> qConsumer(Queue<int, 4>* q, int n, long long* sum) {
    for (int i = 0; i < n; ++i) {
        *sum += co_await q->receive();
    }
}

struct Counted {
    static int live;
    int x;
    explicit Counted(int v) noexcept : x(v) { ++live; }
    Counted(const Counted& o) noexcept : x(o.x) { ++live; }
    Counted(Counted&& o) noexcept : x(o.x) { ++live; }
    Counted& operator=(const Counted&) noexcept = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() { --live; }
};
int Counted::live = 0;

Task<void> countedProducer(Queue<Counted, 4>* q, int n) {
    for (int i = 0; i < n; ++i) {
        co_await q->send(Counted{i});
    }
}
Task<void> countedConsumer(Queue<Counted, 4>* q, int n, long long* sum) {
    for (int i = 0; i < n; ++i) {
        Counted c = co_await q->receive();
        *sum += c.x;
    }
}

} // namespace

int main() {
    auto& sch = scheduler();

    // ---- 100,000+ task completions (heavy slot reuse + generation churn) ----
    g_counter = 0;
    while (g_counter < 100000) {
        for (int i = 0; i < 16; ++i) {
            spawn(trivial());
        }
        int guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 100) {
            poll();
        }
    }
    SA_CHECK(g_counter >= 100000);
    drainAndCheckIdle();

    // ---- repeated deep child nesting ----
    for (int r = 0; r < 500; ++r) {
        spawn(nest(8)); // an 8-deep chain of awaited child tasks
        int guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 1000) {
            poll();
        }
    }
    drainAndCheckIdle();

    // ---- allocator fragmentation / recovery: must exercise coalescing ----
    // Park mixed-size frames until only a thin tail of the pool is free, then
    // release them so their adjacent blocks must coalesce, then place a frame
    // larger than the leftover tail AND larger than any single freed block. Only
    // a coalescing deallocate can satisfy it: with coalescing removed the request
    // hits the deterministic frame_pool_exhausted hook (recorded above and caught
    // by drainAndCheckIdle). This is unlike same-size, same-order churn, which
    // first-fit reuses hole-for-hole and cannot detect a broken coalesce.
    for (int r = 0; r < 100; ++r) {
        Event hold;
        int parked = 0;
        while (stats().frameBytesFree > 1500 && parked < 64) {
            if (parked & 1) {
                spawn(holdSized<192>(&hold));
            } else {
                spawn(holdSized<96>(&hold));
            }
            poll(); // create and park the newly spawned blocker
            ++parked;
        }
        hold.set(); // release every blocker so their adjacent blocks coalesce
        int guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 4000) {
            poll();
        }
        spawn(sized<2048>()); // fits only in a coalesced pool
        guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 4000) {
            poll();
        }
    }
    drainAndCheckIdle();

    // ---- repeated timer creation / completion ----
    for (int r = 0; r < 800; ++r) {
        for (int i = 1; i <= 8; ++i) {
            spawn(napper(static_cast<uint64_t>(i))); // relative durations 1..8 us
        }
        poll();        // nappers run and park with deadlines g_now + 1..8
        g_now += 100;  // advance past every deadline
        int guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 100) {
            poll();
        }
    }
    drainAndCheckIdle();

    // ---- Event fan-out: many waiters released together, repeated ----
    {
        Event ev;
        for (int r = 0; r < 400; ++r) {
            int woken = 0;
            for (int i = 0; i < 16; ++i) {
                spawn(evWaiter(&ev, &woken));
            }
            poll();       // all 16 park on the clear event
            ev.set();     // release all
            int guard = 0;
            while (sch.activeTaskCount() > 0 && guard++ < 100) {
                poll();
            }
            ev.clear();
            SA_CHECK(woken == 16);
        }
    }
    drainAndCheckIdle();

    // ---- ThreadSafeFlag storm: many set/poll wake cycles ----
    {
        ThreadSafeFlag flag;
        int wakes = 0;
        const int iters = 8000;
        spawn(flagConsumer(&flag, iters, &wakes));
        poll(); // consumer parks
        for (int i = 0; i < iters; ++i) {
            flag.set();
            poll(); // resolve the external signal -> one wake -> re-park
        }
        int guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 100) {
            poll();
        }
        SA_CHECK(wakes == iters);
    }
    drainAndCheckIdle();

    // ---- Queue producer/consumer saturation over a small queue ----
    {
        Queue<int, 4> q;
        long long sum = 0;
        const int n = 8000;
        spawn(qProducer(&q, n));
        spawn(qConsumer(&q, n, &sum));
        drainAndCheckIdle();
        SA_CHECK(sum == static_cast<long long>(n) * (n - 1) / 2);
        SA_CHECK(q.empty());
    }

    // ---- Queue churn with a lifetime-counted payload (no double destroy) ----
    {
        SA_CHECK(Counted::live == 0);
        Queue<Counted, 4> q;
        long long sum = 0;
        const int n = 4000;
        spawn(countedProducer(&q, n));
        spawn(countedConsumer(&q, n, &sum));
        drainAndCheckIdle();
        SA_CHECK(sum == static_cast<long long>(n) * (n - 1) / 2);
        SA_CHECK(q.empty());
    }
    SA_CHECK(Counted::live == 0); // every payload destroyed exactly once

    SA_RUN_TESTS();
}
