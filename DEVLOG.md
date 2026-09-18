## Sample commit devlog
STATE: delete table half green; fuzz fails at op 6410, seed 1337.
DID: two-children reduction + one-child splice; transplant helper.
DECIDED: single shared NIL sentinel (not per-leaf) -- validate
counts it once at the bottom of every path. See spec, invariants.
LEARNED: my "case 3" tested the sibling’s wrong child in the
mirrored branch; rb_validate caught it via black-height, not
ordering. Validate-every-100-ops earns its keep.
NEXT FIRST STEP: shrink the seed-1337 failure to <15 ops,
then reread the deletion cases before prompting.
OPEN: on overwrite, old value freed before or after new key
alloc succeeds? (Header says value not consumed on failure;
so: after. Confirm in tests.)

## Commit 1 - Initial Commit
STATE: copied base from spec, rbtree.c, test_rbtree.c, and fuzz.c are empty
DID: copied base from spec
DECIDED: nothing yet :D
LEARNED: nothing yet :D
NEXT FIRST STEP: get base node structure and create/insert/find/validate
OPEN: idk

Told claude to plan out the initial tests, and not worry about fuzz yet.

Decided to use a parent pointer for the nodes, so that I don't have to go back into the node creation places if I need it in the future, despite it not being needed at the moment. Also, it is commonly assumed to be present in fixup algorithms, and it would likely make that easier as well.

Added a comment standard in CLAUDE.md so that any tests generated will have comments for what bugs/cases they are specifically checking for, so that claude is forced to think at a larger scale, and I can challenge its reasoning easier.

Approved a test not explicitly defined in the header, and also laid out the foundation for using a null check for every public function, since I think this will help a lot during the development process, although it isn't something I'm necessarily planning to keep throughout the entire development, only for debugging.

## Commit 2 - Tests Complete
STATE: 7 tests are in test_rbtree.c, with comments stating what they test and what risks they guard from. rbtree.c is still empty.
DID: all 7 tests: test_create_empty, test_insert_find_mixed_order, test_insert_duplicate_overwrites_and_frees_old_value, test_validate_ordering_on_populated_tree, test_foreach_visits_in_increasing_order, test_destroy_frees_owned_values, test_null_tree_is_safe_everywhere
DECIDED: use parent pointer, add comments for all tests, add test for checking for null in all public functions
NEXT FIRST STEP: start implementing rbtree.c based on the tests