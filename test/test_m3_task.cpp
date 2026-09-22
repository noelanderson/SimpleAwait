// M3 Task<void> ownership test.
//
// Verifies the lazy, move-only Task<void>: calling a Task-returning coroutine
// creates a frame FROM THE POOL (never the global heap) and does not run the
// body; moving transfers the single ownership token; a moved-from/empty Task is
// harmless; and destroying an unscheduled Task returns its frame to the pool
// (full recovery). A global operator new/delete canary proves no coroutine frame
// touches the global heap.

#include <cstddef>
#include <cstdlib>
#include <utility>

namespace {
unsigned long long g_global_new_calls = 0;
}

void* operator new(std::size_t n) { ++g_global_new_calls; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n) { ++g_global_new_calls; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::Task;
using simpleawait::detail::frame_pool;

namespace {
bool g_body_ran = false;

Task<void> make_task() {
    g_body_ran = true; // must never run in M3 (Task is never resumed)
    co_return;
}
} // namespace

int main() {
    const std::size_t baseUsed = frame_pool().bytesUsed();

    // ---- lazy creation: frame from the pool, body not run, no global heap ----
    {
        g_body_ran = false;
        const unsigned long long newBefore = g_global_new_calls;
        Task<void> t = make_task();
        SA_CHECK(static_cast<bool>(t));                    // owns a frame
        SA_CHECK(!g_body_ran);                             // lazy: body not run
        SA_CHECK(frame_pool().bytesUsed() > baseUsed);     // frame came from the pool
        SA_CHECK(g_global_new_calls == newBefore);         // NOT from the global heap
    }
    SA_CHECK(frame_pool().bytesUsed() == baseUsed);        // unscheduled dtor recovered it
    SA_CHECK(!g_body_ran);

    // ---- move constructor transfers the single ownership token ----
    {
        Task<void> a = make_task();
        SA_CHECK(static_cast<bool>(a));
        Task<void> b = std::move(a);
        SA_CHECK(!static_cast<bool>(a)); // moved-from is empty
        SA_CHECK(static_cast<bool>(b));  // b now owns the frame
    }                                    // only b destroys it -> no double destroy
    SA_CHECK(frame_pool().bytesUsed() == baseUsed);

    // ---- move assignment frees the previous frame and takes the source's ----
    {
        Task<void> a = make_task();
        Task<void> b = make_task();
        SA_CHECK(frame_pool().bytesUsed() > baseUsed);
        b = std::move(a);
        SA_CHECK(!static_cast<bool>(a));
        SA_CHECK(static_cast<bool>(b));
    }
    SA_CHECK(frame_pool().bytesUsed() == baseUsed);

    // ---- default and moved-from Task are empty with harmless destructors ----
    {
        Task<void> empty;
        SA_CHECK(!static_cast<bool>(empty));

        Task<void> src = make_task();
        Task<void> dst = std::move(src);
        SA_CHECK(!static_cast<bool>(src)); // harmless to destroy the moved-from one
        SA_CHECK(static_cast<bool>(dst));
    }
    SA_CHECK(frame_pool().bytesUsed() == baseUsed);

    // ---- many create/destroy cycles fully recover the pool (no leak) ----
    {
        const unsigned long long newBefore = g_global_new_calls;
        for (int i = 0; i < 200; ++i) {
            Task<void> t = make_task();
            SA_CHECK(static_cast<bool>(t));
        }
        SA_CHECK(g_global_new_calls == newBefore); // still no global heap use
    }
    SA_CHECK(frame_pool().bytesUsed() == baseUsed);
    SA_CHECK(frame_pool().allocationFailures() == 0); // never exhausted at this pool size

    SA_RUN_TESTS();
}
