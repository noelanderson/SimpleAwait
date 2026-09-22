// M11 stress suite — V1 hardening (IMPLEMENTATION_PLAN M11).
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
namespace { unsigned long long g_now = 0; }
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)

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

// Drain the scheduler to idle (bounded), then assert the M11 final invariants.
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

    // ---- allocator fragmentation / recovery (mixed frame sizes, out-of-order) ----
    for (int r = 0; r < 800; ++r) {
        spawn(sized<24>());
        spawn(sized<200>());
        spawn(sized<64>());
        spawn(sized<120>());
        int guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 100) {
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
