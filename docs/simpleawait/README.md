# SimpleAwait specification

This directory holds the normative specification for SimpleAwait. For usage,
installation, and examples, see the [top-level README](../../README.md).

- [`V1_API_CONTRACT.md`](V1_API_CONTRACT.md) — the frozen public API surface
  (types, free functions, and configuration macros).
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — the normative design: the scheduler
  model and `poll()` contract, coroutine-frame ownership, the fixed frame
  allocator, the monotonic clock, and the synchronization primitives.
- [`SimpleAwait_Implementation_Spec.md`](SimpleAwait_Implementation_Spec.md) —
  implementation notes and rationale behind the design.
- [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) — the staged plan followed
  during initial development, retained as a historical record.

When the code and these documents disagree, the code is authoritative for
behavior and the API contract is authoritative for the public surface; update
them together.
