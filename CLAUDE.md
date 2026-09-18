# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A CS370 lab: implement a red-black tree (string keys, `void *` values) in C23 against a fixed header
contract. `include/rbtree.h`, `src/rbtree.c`, `tests/test_rbtree.c`, and `tests/fuzz.c` currently exist
but are empty — the implementation and tests are the work to be done.

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

## Style
- C23. `-Wall -Wextra -Werror` must stay clean. No VLAs.
- Error handling: goto-cleanup pattern for multi-allocation functions.
- Prefer the smallest diff that passes. Do not refactor unrelated code.
- Every non-obvious loop gets a one-line invariant comment.

## Workflow
- For any multi-file or algorithmic change: propose a plan and wait for
  approval before editing.
- Commit only from a green state; message format "M<n>: <what>".

## Architecture: the contract in `include/rbtree.h`

Everything about `src/rbtree.c` is driven by the ownership and error-handling rules spelled out in the
header's comments — read them closely before implementing or editing any function:

- **Key/value ownership**: `rb_insert` copies the key (tree owns the copy) but only takes ownership of
  `value` on success; on `-1` (allocation failure) the tree is unchanged and the caller still owns
  `value`. Overwriting an existing key frees the old value via the `rb_value_free_fn` passed to
  `rb_create` (which may be `NULL` if values aren't owned).
- **Return-code convention**: `0`/`-1` for success/failure on `rb_insert` and `rb_delete`;
  `rb_validate` returns `0` iff all red-black invariants hold; `rb_find` returns the value or `NULL`.
- **`rb_destroy`** must be NULL-safe and free all nodes, key copies, and owned values.
- `rb_foreach` performs an in-order traversal, calling `fn(key, value, ctx)` for each entry.

`tests/test_rbtree.c` is the unit-test suite (linked with `src/rbtree.c`); `tests/fuzz.c` takes an
iteration count (see `./build/fuzz 100000` in `make test`) and is used for randomized/stress testing,
including under ASan/UBSan (`make asan`) and Valgrind (`make memcheck`).
