# SlotList stores its links inline in each slot (not `std::list`)

Status: accepted

## Context

Under the `Noexcept` transfer policy, `PoolAllocator`'s stores must merge their
contents into another pool **without allocating**, because the merge runs inside
a destructor (BayaRepo's `~ScopedFlitPool`, issue #15735) where a throw is
`std::terminate`. Two of the operations are also on the hot path: a free slot is
added by `deallocate`, which is `noexcept` and called millions of times.

## Decision

`SlotList<T>` is a singly-linked list whose `next` pointer is stored **inline in
each slot's own bytes** (written/read with `std::memcpy`, so no alignment
assumption). There are no separately allocated link nodes, so `push`, `pop`, and
`splice` allocate nothing and are `noexcept`.

Both stores use `SlotList` under the `Noexcept` policy:
- The free-slot store threads the link through each freed slot's bytes (the slot
  is dead while free, so its storage is available).
- The block store reserves **slot 0** of each block for the block-chain link and
  carves usable slots from slot 1 onward (a block holds live objects, so the
  link can't live in arbitrary block bytes — only in a permanently reserved
  cell).

## Considered options

- **`std::list<T*>` — rejected.** `push_back` allocates a node, which can throw.
  On the free-slot store that node allocation lands in the `noexcept`
  `deallocate` (→ `terminate`) and on the hot path (a heap allocation per
  deallocate). `splice` being `noexcept` doesn't save it — the fatal cost is on
  the add path, not the merge.
- **`std::vector<T*>` — kept, but only for the `Fast` policy.** `splice` is
  `insert`, which can reallocate and throw. Fine when transfer is allowed to
  throw; unusable for the `noexcept` guarantee.
- **Pre-reserving vector capacity — rejected.** The required capacity is not
  knowable inside a generic allocator, and `reserve` itself throws — it moves
  the failure, it doesn't remove it.

## Consequences

- Requires `sizeof(T) >= sizeof(T*)`. Types smaller than a pointer (`int`,
  `char`) cannot use `SlotList` and fall back to the `Fast` / `SlotVector`
  backend. The `static_assert` lives inside `SlotList`, so incomplete/forward-
  declared types on the default `Fast` path are unaffected.
- Costs roughly 2.4x on the alloc/deallocate hot path versus the vector backend
  (pointer-chasing vs. contiguous). Accepted only because it is opt-in via the
  `Noexcept` policy; `Fast` (vector) remains the default.
- **Do not replace `SlotList` with `std::list` "for simplicity."** It compiles
  and passes ordinary tests, but silently reintroduces a throwing transfer,
  which `terminate`s inside `~ScopedFlitPool` under OOM. This ADR exists so that
  swap is never made unknowingly.
