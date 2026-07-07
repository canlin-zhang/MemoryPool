# Transfer uses a spinlock, not `std::mutex`, so it can be `noexcept`

Status: accepted

## Context

`PoolAllocator` serializes concurrent transfers into a destination pool with a
per-instance lock (many worker threads may `transfer_all` their own source pool
into one shared destination; see the `TransferConcurrency` test). Under
`TransferMode::Noexcept`, `transfer_all`/`transfer_free` are declared `noexcept`
because they run where a throw is fatal — e.g. inside BayaRepo's
`~ScopedFlitPool` destructor (issue #15735).

`std::mutex::lock()` is *permitted to throw* `std::system_error`. A `noexcept`
function that takes a `std::lock_guard<std::mutex>` therefore contains a
potentially-throwing operation; on lock failure it would `std::terminate`. The
list backend already makes the transfer *work* allocation-free (see
`0001-slotlist-inline-links.md`), so the lock was the only remaining throw site
standing between us and an airtight `noexcept`.

## Decision

Protect the transfer with a `std::atomic<bool>` spinlock (`SpinLockGuard`)
instead of `std::mutex`, for **both** transfer modes (there is no per-mode lock
split — a single spinlock member serves Fast and Noexcept alike). `std::atomic`
`compare_exchange`/`store` are `noexcept`, so acquiring and releasing the lock
cannot throw. Under `Noexcept` that is what makes the transfer genuinely
`noexcept`; under `Fast` the same lock is used and the transfer can still throw,
but only from the vector splice, never from the lock. The critical section is
only a few pointer splices (O(1)), and transfers are rare phase-boundary events,
so the lock is essentially always uncontended and spinning is cheap.

## Considered options

- **`std::mutex` + `std::lock_guard` — rejected.** `lock()` may throw
  `system_error`, so the `noexcept` promise would be a lie that `terminate`s on
  lock failure. On glibc a normal mutex locked once won't actually throw, but
  "won't throw in practice" is not the guarantee a destructor-safe API should
  rest on.
- **Drop the lock entirely — rejected.** Our fork deliberately supports
  concurrent transfers into one destination (the `TransferConcurrency` test).
  The spinlock keeps that guarantee at negligible cost, so there's no reason to
  give it up to reach `noexcept`.
- **`try_lock` spin on `std::mutex` — rejected.** Still carries a `std::mutex`
  for no benefit over an atomic spinlock, and reads worse.

## Consequences

- Under contention the lock busy-waits rather than sleeping. Acceptable: the
  critical section is tiny and contention is rare (phase boundaries), never a
  hot path. It is not a general-purpose mutex and should not be used to guard
  long or frequently-contended sections.
- No fairness or priority inheritance (things `std::mutex` may offer). Irrelevant
  for a section this short and rarely contended.
- With the list backend and this spinlock, `transfer_all`/`transfer_free` have
  no remaining throw site and are genuinely `noexcept` under `TransferMode::Noexcept`.
