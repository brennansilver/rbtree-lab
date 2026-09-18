# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A CS370 lab: implement a red-black tree (string keys, `void *` values) in C23 against a fixed header contract. 
`src/rbtree.c`, `tests/test_rbtree.c`, and `tests/fuzz.c` currently exist but are empty — the implementation and tests are the work to be done.

## Commands
- Build & unit tests: `make test`
- Sanitizers: `make asan`  Valgrind: `make memcheck`
- A change is DONE only when all three pass. Always run them; show output.
- `make clean` removes `build/`.

## Hard constraints
- NEVER modify `include/rbtree.h`. It is the graded contract.
- Check every allocation. malloc can return NULL; a NULL return must
  leave the tree unchanged and return the documented error code.
- NEVER weaken, skip, or delete a test to make the suite pass. If a test
  looks wrong, stop and explain why instead.
- All heap allocation in `rbtree.c` goes through the `rb_malloc`/`rb_free`
  wrappers (thin forwards to `malloc`/`free`) — never call `malloc`/`free`
  directly.

## Style
- C23. `-Wall -Wextra -Werror` must stay clean. No VLAs.
- No goto, unless cleanup-label unwinding.
- Prefer the smallest diff that passes. Do not refactor unrelated code.
- Every non-obvious loop gets a one-line invariant comment.
- Add comments to large structural portions of code (such as why a node is being recolored where it is, potential breaking points, etc). It is better for things to be over-explained than under-explained.
- Every test function gets a brief comment directly above it stating what
  it tests and what specific bug(s)/risk(s) it's meant to catch — not a
  restatement of what the code visibly does.

## Workflow
- For any multi-file or algorithmic change: propose a plan and wait for
  approval before editing.
- Commit only from a green state; message format "M<n>: <what>".

## Architecture: the contract in `include/rbtree.h`

Everything about `src/rbtree.c` is driven by the ownership and error-handling rules spelled out in the header's comments — read them closely before implementing or editing any function:

- **Key/value ownership**: `rb_insert` copies the key (tree owns the copy) but only takes ownership of
  `value` on success; on `-1` (allocation failure) the tree is unchanged and the caller still owns
  `value`. Overwriting an existing key frees the old value via the `rb_value_free_fn` passed to `rb_create` (which may be `NULL` if values aren't owned).
- **Return-code convention**: `0`/`-1` for success/failure on `rb_insert` and `rb_delete`;
  `rb_validate` returns `0` iff all red-black invariants hold; `rb_find` returns the value or `NULL`.
- **`rb_destroy`** must be NULL-safe and free all nodes, key copies, and owned values.
- `rb_foreach` performs an in-order traversal, calling `fn(key, value, ctx)` for each entry.

`tests/test_rbtree.c` is the unit-test suite (linked with `src/rbtree.c`); 
`tests/fuzz.c` takes an iteration count (see `./build/fuzz 100000` in `make test`) and is used for randomized/stress testing, including under ASan/UBSan (`make asan`) and Valgrind (`make memcheck`).
- Fuzzer: ≥10^5 random insert/find/delete ops against a reference model
  (sorted array or linked list), calling `rb_validate` at least every
  100 ops.
- `rb_delete` unit tests: table-driven, covering at minimum a red leaf, a
  black leaf with a red sibling, a two-children node, and root deletion —
  each asserting `rb_validate` and `rb_size` afterward.

### Red-black invariants (`rb_validate`)
`rb_validate` returns 0 iff all of the following hold:
1. The root is black.
2. No red node has a red child.
3. Every root-to-NIL path has the same number of black nodes (black-height).
4. In-order traversal yields strictly increasing keys under `strcmp`.
5. `rb_size` matches the actual node count.

As of the M1 (struct/create/insert-no-fixup/find/destroy) commit, only
invariant 4 is implemented — `rb_insert` doesn't rotate/recolor yet, so
1/2/3 would spuriously fail on any tree of size > 2. Invariants 1/2/3/5
land once fixup and delete are implemented.

### Sentinel design
Each `rbtree_t` owns a single shared NIL sentinel, embedded (not
heap-allocated) as a field of `struct rbtree` and always colored black.
Every leaf child pointer points at that sentinel instead of `NULL`. This
keeps multiple trees' sentinels independent and avoids repeated NULL
checks in fixup/delete code.
