#pragma once

// SimpleAwait — Task<void>: a lazy, move-only coroutine handle.
//
// Calling a Task-returning coroutine creates the coroutine frame (allocated from
// the fixed frame pool, never the global heap) and immediately suspends at
// initial_suspend — the body does NOT run until the Task is scheduled
// (create_task/spawn, later milestone) or awaited (co_await, later milestone).
//
// Ownership is a single token: exactly one Task owns a live frame. Moving
// transfers the token and empties the source; an empty/moved-from Task owns
// nothing and its destructor is a no-op. Destroying a Task that still owns an
// (unscheduled) frame destroys that frame exactly once, returning its bytes to
// the pool.
//
// Only Task<void> is functional in V1 (per docs/simpleawait/V1_API_CONTRACT.md
// §4). Instantiating Task<T> for any other T is a clear compile error.

#include <coroutine>
#include <cstddef>
#include <new>

#include "config.h"
#include "error.h"
#include "detail/global_frame_pool.h"

namespace simpleawait {

template <class T = void>
class Task;

namespace detail {
template <class>
inline constexpr bool task_type_unsupported = false;

// Transfers the coroutine frame out of a Task, leaving the Task empty. Used by
// the scheduler to take ownership when a Task is scheduled. Defined after
// Task<void>.
std::coroutine_handle<> take_frame(Task<void>& task) noexcept;

// Returns true if the child was adopted by the scheduler (the awaiting coroutine
// must stay suspended); false if the awaiting coroutine is not the currently
// running SimpleAwait task (the error was reported and the caller must resume
// rather than hang). `awaiting` is the coroutine actually suspending on this
// await. Defined in scheduler.h.
bool start_child(std::coroutine_handle<> child, std::coroutine_handle<> awaiting) noexcept;

// Awaiter for `co_await` on a Task<void> (sequential child await). It is move-only
// and RAII-owning: from the moment `operator co_await` consumes the Task until the
// frame is handed to the scheduler in await_suspend, this awaiter is the child
// frame's sole owner, so an awaiter that is extracted but never awaited releases
// the frame in its destructor instead of leaking it. Awaiting an empty Task
// (default-constructed or already consumed) fails deterministically via the error
// hook (task_awaited_twice) without suspending.
class TaskAwaiter {
public:
    explicit TaskAwaiter(std::coroutine_handle<> child) noexcept
        : child_(child), was_empty_(child == nullptr) {}

    TaskAwaiter(TaskAwaiter&& other) noexcept
        : child_(other.child_), was_empty_(other.was_empty_) {
        other.child_ = {};
    }
    TaskAwaiter(const TaskAwaiter&) = delete;
    TaskAwaiter& operator=(const TaskAwaiter&) = delete;
    TaskAwaiter& operator=(TaskAwaiter&&) = delete;
    ~TaskAwaiter() {
        if (child_) {
            child_.destroy(); // abandoned before suspension: release the frame
        }
    }

    bool await_ready() const noexcept { return was_empty_; }
    bool await_suspend(std::coroutine_handle<> awaiting) noexcept {
        const std::coroutine_handle<> child = child_;
        child_ = {}; // hand ownership to the scheduler (or its failure path)
        // Pass the actual awaiting coroutine so the scheduler can confirm it is
        // the running SimpleAwait task before adoption; a foreign or nested
        // coroutine must not be attached to an unrelated parent.
        return start_child(child, awaiting);
    }
    void await_resume() const noexcept {
        if (was_empty_) {
            SIMPLEAWAIT_ON_ERROR(Error::task_awaited_twice);
        }
    }

private:
    std::coroutine_handle<> child_;
    bool was_empty_;
};
} // namespace detail

// Primary template: any Task<T> other than Task<void> is unsupported in V1.
template <class T>
class Task {
    static_assert(detail::task_type_unsupported<T>,
                  "SimpleAwait V1 supports only Task<void>");
};

template <>
class Task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;

    Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = {}; }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            // Take the source's frame and empty the source BEFORE destroying our
            // previous frame. The source may live inside that frame (e.g. a
            // by-value coroutine parameter); destroying our frame first would run
            // the source's destructor and then leave us reading a destroyed
            // source (UB / silently lost work).
            const handle_type previous = handle_;
            handle_ = other.handle_;
            other.handle_ = {};
            if (previous) {
                previous.destroy();
            }
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() { reset(); }

    // True iff this Task owns a coroutine frame.
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // Sequential child await: `co_await foo()` runs the child Task to completion
    // as a child of the awaiting task, then resumes the parent on a LATER poll
    // pass (no inline resume, no symmetric transfer). Awaiting consumes this Task
    // (rvalue-qualified); a second await of the now-empty Task fails
    // deterministically via the error hook.
    detail::TaskAwaiter operator co_await() && noexcept {
        const std::coroutine_handle<> child = handle_;
        handle_ = {};
        return detail::TaskAwaiter{child};
    }

private:
    explicit Task(handle_type h) noexcept : handle_(h) {}

    friend std::coroutine_handle<> detail::take_frame(Task<void>&) noexcept;

    // Destroy the owned frame, if any, exactly once. Destroying a suspended
    // (never-resumed) coroutine still runs its by-value parameters' destructors
    // (but not the body); detach handle_ FIRST so that if such a destructor
    // re-enters this Task (e.g. assigns it empty), the re-entry sees an empty
    // handle and is a no-op instead of a double destroy.
    void reset() noexcept {
        if (const handle_type h = handle_) {
            handle_ = {};
            h.destroy();
        }
    }

    handle_type handle_{};
};

struct Task<void>::promise_type {
    // Lazy: the coroutine suspends before its body runs.
    Task<void> get_return_object() noexcept {
        return Task<void>{handle_type::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {
        SIMPLEAWAIT_ON_ERROR(Error::unhandled_exception);
    }

    // Coroutine frames are allocated from the fixed pool, never the global heap.
    // On exhaustion the deterministic error hook is invoked with
    // frame_pool_exhausted; if a non-halting override returns, operator new
    // yields nullptr and the coroutine returns the empty Task from
    // get_return_object_on_allocation_failure (no exception is thrown).
    static void* operator new(std::size_t size) noexcept { return frame_alloc(size); }
    static void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
        return frame_alloc(size);
    }
    static void operator delete(void* p) noexcept {
        detail::frame_pool().deallocate(p);
    }
    static void operator delete(void* p, std::size_t) noexcept {
        detail::frame_pool().deallocate(p);
    }
    static Task<void> get_return_object_on_allocation_failure() noexcept {
        return Task<void>{};
    }

private:
    static void* frame_alloc(std::size_t size) noexcept {
        void* p = detail::frame_pool().allocate(size);
        if (p == nullptr) {
            SIMPLEAWAIT_ON_ERROR(Error::frame_pool_exhausted);
        }
        return p; // reachable via the success path; no unreachable code after the hook
    }
};

namespace detail {
inline std::coroutine_handle<> take_frame(Task<void>& task) noexcept {
    const std::coroutine_handle<> frame = task.handle_;
    task.handle_ = {};
    return frame;
}
} // namespace detail

} // namespace simpleawait
