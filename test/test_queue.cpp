// Queue<T,N> test — bounded scheduler-local FIFO with blocking send/receive.
//
// Covers basic try/await send+receive, empty-receive and full-send suspension
// (wakeups deferred, never inline), FIFO value order, FIFO sender and receiver
// waiter order, ring-index wrapping, move-only and non-default-constructible
// payloads, exactly-once payload construction/destruction (including destroying a
// non-empty queue), and a long producer/consumer stress. A global new/delete canary
// proves the queue mechanics (with trivial payloads) touch no global heap.

#include <cstddef>
#include <cstdlib>
#include <memory>

namespace { unsigned long long g_new_calls = 0; }
void* operator new(std::size_t n) { ++g_new_calls; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n) { ++g_new_calls; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace { unsigned long long g_now = 0; }
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::poll;
using simpleawait::Queue;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Task;

namespace {

template <size_t N>
Task<void> intReceiver(Queue<int, N>* q, int* out) {
    *out = co_await q->receive();
}
template <size_t N>
Task<void> intSender(Queue<int, N>* q, int val, int* phase) {
    co_await q->send(val);
    *phase = val; // set only once the send completes
}

Task<void> intProducer(Queue<int, 4>* q, int n) {
    for (int i = 0; i < n; ++i) {
        co_await q->send(i);
    }
}
Task<void> intConsumer(Queue<int, 4>* q, int n, long* sum) {
    for (int i = 0; i < n; ++i) {
        const int v = co_await q->receive();
        *sum += v;
    }
}

// Move-only payload.
Task<void> upProducer(Queue<std::unique_ptr<int>, 1>* q, int v) {
    co_await q->send(std::make_unique<int>(v));
}
Task<void> upConsumer(Queue<std::unique_ptr<int>, 1>* q, int* out) {
    std::unique_ptr<int> p = co_await q->receive();
    *out = *p;
}

// Non-default-constructible payload (no default constructor).
struct NoDefault {
    int x;
    explicit NoDefault(int v) noexcept : x(v) {}
};
Task<void> ndcConsumer(Queue<NoDefault, 1>* q, int* out) {
    NoDefault r = co_await q->receive(); // constructed into awaiter storage, no default ctor
    *out = r.x;
}

// Copy-only payload: has a copy constructor but an explicitly DELETED move
// constructor. Delivery must fall back to copy on every path (send and receive)
// rather than force the deleted move.
struct CopyOnly {
    int x;
    explicit CopyOnly(int v) noexcept : x(v) {}
    CopyOnly(const CopyOnly& o) noexcept : x(o.x) {}
    CopyOnly& operator=(const CopyOnly& o) noexcept {
        x = o.x;
        return *this;
    }
    CopyOnly(CopyOnly&&) = delete;
    CopyOnly& operator=(CopyOnly&&) = delete;
};
Task<void> copyOnlySender(Queue<CopyOnly, 1>* q, int v, int* phase) {
    co_await q->send(CopyOnly{v});
    *phase = v;
}
Task<void> copyOnlyReceiver(Queue<CopyOnly, 1>* q, int* out) {
    CopyOnly r = co_await q->receive();
    *out = r.x;
}

// Lifetime-counting payload: proves construction/destruction balance exactly.
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

} // namespace

int main() {
    auto& sch = scheduler();
    const unsigned long long newAtStart = g_new_calls;

    // ---- basic trySend/tryReceive + observers ----
    {
        Queue<int, 4> q;
        SA_CHECK(q.empty() && !q.full() && q.size() == 0 && q.capacity() == 4);
        SA_CHECK(q.trySend(10));
        SA_CHECK(q.trySend(20));
        SA_CHECK(q.size() == 2 && !q.empty() && !q.full());
        int v = 0;
        SA_CHECK(q.tryReceive(v) && v == 10);
        SA_CHECK(q.tryReceive(v) && v == 20);
        SA_CHECK(!q.tryReceive(v)); // empty now
        SA_CHECK(q.empty());
    }

    // ---- full trySend fails without blocking ----
    {
        Queue<int, 2> q;
        SA_CHECK(q.trySend(1) && q.trySend(2));
        SA_CHECK(q.full());
        SA_CHECK(!q.trySend(3)); // full: reported, not buffered
        SA_CHECK(q.size() == 2);
    }

    // ---- empty receive suspends; a later send hands off and wakes (deferred) ----
    {
        Queue<int, 2> q;
        int got = -1;
        spawn(intReceiver<2>(&q, &got));
        poll(); // receiver parks on the empty queue
        SA_CHECK(got == -1);
        SA_CHECK(q.trySend(42)); // direct hand-off to the waiting receiver
        SA_CHECK(got == -1);     // NOT resumed inline
        SA_CHECK(q.empty());     // handed off, never buffered
        poll();                  // receiver runs
        SA_CHECK(got == 42);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- full send suspends; a receive frees a slot, admits the sender (deferred) ----
    {
        Queue<int, 2> q;
        SA_CHECK(q.trySend(1) && q.trySend(2)); // full
        int sphase = -1;
        spawn(intSender<2>(&q, 3, &sphase));
        poll(); // sender parks (full), value 3 held on its frame
        SA_CHECK(sphase == -1 && q.full());
        int v = 0;
        SA_CHECK(q.tryReceive(v) && v == 1); // frees a slot -> admit sender(3)
        SA_CHECK(sphase == -1);              // sender NOT resumed inline
        SA_CHECK(q.full());                  // slot refilled with the sender's value
        poll();                              // sender completes
        SA_CHECK(sphase == 3);
        SA_CHECK(q.tryReceive(v) && v == 2); // FIFO: 2 before 3
        SA_CHECK(q.tryReceive(v) && v == 3);
        SA_CHECK(q.empty() && sch.activeTaskCount() == 0);
    }

    // ---- FIFO sender waiter order ----
    {
        Queue<int, 1> q;
        SA_CHECK(q.trySend(100)); // full (capacity 1)
        int pa = -1, pb = -1, pc = -1;
        spawn(intSender<1>(&q, 101, &pa));
        spawn(intSender<1>(&q, 102, &pb));
        spawn(intSender<1>(&q, 103, &pc));
        poll(); // all three park in order
        int v = 0;
        SA_CHECK(q.tryReceive(v) && v == 100); // admit oldest sender (101)
        poll();
        SA_CHECK(pa == 101 && pb == -1 && pc == -1);
        SA_CHECK(q.tryReceive(v) && v == 101);
        poll();
        SA_CHECK(pb == 102 && pc == -1);
        SA_CHECK(q.tryReceive(v) && v == 102);
        poll();
        SA_CHECK(pc == 103);
        SA_CHECK(q.tryReceive(v) && v == 103);
        SA_CHECK(q.empty() && sch.activeTaskCount() == 0);
    }

    // ---- FIFO receiver waiter order ----
    {
        Queue<int, 2> q;
        int ra = -1, rb = -1, rc = -1;
        spawn(intReceiver<2>(&q, &ra));
        spawn(intReceiver<2>(&q, &rb));
        spawn(intReceiver<2>(&q, &rc));
        poll(); // all three park in order
        SA_CHECK(q.trySend(1)); // -> oldest receiver
        SA_CHECK(q.trySend(2)); // -> next receiver
        poll();
        SA_CHECK(ra == 1 && rb == 2 && rc == -1);
        SA_CHECK(q.trySend(3)); // -> last receiver
        poll();
        SA_CHECK(rc == 3);
        SA_CHECK(q.empty() && sch.activeTaskCount() == 0);
    }

    // ---- ring index wrapping ----
    {
        Queue<int, 3> q;
        int v = 0;
        for (int i = 0; i < 10; ++i) { // wraps several times
            SA_CHECK(q.trySend(i));
            SA_CHECK(q.tryReceive(v) && v == i);
        }
        SA_CHECK(q.empty());
        SA_CHECK(q.trySend(1) && q.trySend(2));
        SA_CHECK(q.tryReceive(v) && v == 1);
        SA_CHECK(q.trySend(3) && q.trySend(4)); // tail wraps past the end
        SA_CHECK(q.tryReceive(v) && v == 2);
        SA_CHECK(q.tryReceive(v) && v == 3);
        SA_CHECK(q.tryReceive(v) && v == 4);
        SA_CHECK(q.empty());
    }

    // ---- long producer/consumer stress over a small queue ----
    {
        Queue<int, 4> q;
        long sum = 0;
        const int N = 1000;
        spawn(intProducer(&q, N));
        spawn(intConsumer(&q, N, &sum));
        int guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 200000) {
            poll();
        }
        SA_CHECK(sch.activeTaskCount() == 0);
        SA_CHECK(sum == static_cast<long>(N) * (N - 1) / 2); // 0+1+...+(N-1)
        SA_CHECK(q.empty());
    }

    // The trivial-payload queue mechanics allocated nothing on the global heap.
    SA_CHECK(g_new_calls == newAtStart);

    // ---- move-only payload (unique_ptr): try and await paths ----
    {
        Queue<std::unique_ptr<int>, 2> q;
        SA_CHECK(q.trySend(std::make_unique<int>(7)));
        std::unique_ptr<int> p;
        SA_CHECK(q.tryReceive(p) && p && *p == 7);
    }
    {
        Queue<std::unique_ptr<int>, 1> q;
        int got = -1;
        spawn(upConsumer(&q, &got)); // parks (empty)
        poll();
        spawn(upProducer(&q, 9));    // hands the moved value to the receiver
        poll();
        poll();
        SA_CHECK(got == 9);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- non-default-constructible payload via coroutine receive ----
    {
        Queue<NoDefault, 1> q;
        int got = -1;
        spawn(ndcConsumer(&q, &got));
        poll(); // parks (empty)
        SA_CHECK(q.trySend(NoDefault{55}));
        poll();
        SA_CHECK(got == 55);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- copy-only payload (deleted move ctor): every delivery path copies ----
    {
        // immediate buffer, via both trySend overloads, drained by tryReceive
        Queue<CopyOnly, 2> q;
        SA_CHECK(q.trySend(CopyOnly{11})); // trySend(T&&) copies (no move ctor)
        const CopyOnly lv{12};
        SA_CHECK(q.trySend(lv));           // trySend(const T&) copies
        CopyOnly out{0};
        SA_CHECK(q.tryReceive(out) && out.x == 11);
        SA_CHECK(q.tryReceive(out) && out.x == 12);
        SA_CHECK(q.empty());
    }
    {
        // parked-sender path: co_await send on a full queue, admitted by copy
        Queue<CopyOnly, 1> q;
        SA_CHECK(q.trySend(CopyOnly{20})); // full
        int sp = -1;
        spawn(copyOnlySender(&q, 21, &sp));
        poll(); // sender parks holding its copied value
        SA_CHECK(sp == -1 && q.full());
        CopyOnly out{0};
        SA_CHECK(q.tryReceive(out) && out.x == 20); // admits the sender (copy to tail)
        poll();
        SA_CHECK(sp == 21);
        SA_CHECK(q.tryReceive(out) && out.x == 21);
        SA_CHECK(q.empty() && sch.activeTaskCount() == 0);
    }
    {
        // direct-receiver path: a parked receiver, then trySend hands off by copy
        Queue<CopyOnly, 1> q;
        int got = -1;
        spawn(copyOnlyReceiver(&q, &got));
        poll(); // receiver parks (empty)
        SA_CHECK(q.trySend(CopyOnly{30})); // direct copy hand-off to the receiver
        poll();
        SA_CHECK(got == 30);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- exactly-once construction/destruction, including a non-empty destroy ----
    SA_CHECK(Counted::live == 0);
    {
        Queue<Counted, 3> q;
        SA_CHECK(q.trySend(Counted{1}));
        SA_CHECK(q.trySend(Counted{2}));
        SA_CHECK(q.trySend(Counted{3}));
        {
            Counted out{0};
            SA_CHECK(q.tryReceive(out) && out.x == 1);
        } // out destroyed
        // q still holds 2 and 3; ~Queue must destroy exactly those.
    }
    SA_CHECK(Counted::live == 0); // no leak, no double-destroy

    SA_RUN_TESTS();
}
