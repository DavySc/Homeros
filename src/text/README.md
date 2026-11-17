# Package 1: the text rope (build B)

`text.c` implements `include/text.h` as a B+ tree of 1 KB nodes, the shape of
ropey (Helix), which DESIGN.md names as the reference. Leaves hold up to 1,008
bytes; inner nodes hold up to 42 children with each child's byte and `'\n'`
counts. Every node but the root is at least half full, so an edit or a line
lookup walks O(log n) nodes.

- **Insert** cuts the text into pieces of at most one leaf and puts each where
  it goes; a full node splits evenly in two. The nodes one piece can need (a
  split on every level and a new root) are allocated before the tree is
  touched, so out of memory returns -1 with the text as it was (pieces already
  in are deleted again).
- **Delete** drops the subtrees inside the range whole and cuts the two at its
  ends. Only the nodes on the paths to the bytes either side of the cut can be
  left under half full; `mend` walks those two paths and merges each such node
  with a neighbour, or evens the two out when they do not fit in one.
- **Line lookups** descend by the `'\n'` counts and scan one leaf.

## Checks

| Check | What it shows | Tag |
| --- | --- | --- |
| `tests/test_text.c` (thread 0) | 200,000 small edits match an array; every line lookup | measured, 1 run per `make test` |
| `tests/fuzz_text_rope.c` | libFuzzer against `ref/text.c` itself, with inserts and deletes up to 64 KB, texts up to 1 MB, reads and both line lookups after every op, out-of-range calls refused | measured: 300 s, 36,368 inputs, nothing found (after the 2 bugs below were fixed) |
| `tests/test_text_rope.c` | the tree's shape after every one of 3,000 edits from 1 byte to 256 KB (every leaf at one depth, every non-root node at least half full, every count equal to its subtree's) and the content against an array; out of memory at every malloc of a 200 KB insert leaves the text unchanged and usable | measured, 1 run per `make test` |
| `tests/plants/text.txt` | 6 plants, one per check and per rule above; `make planted` catches all 6 | measured |

The fuzzer found two bugs in the first version, both fixed and now covered by
plants: deleting the whole text left an inner root with no children, and the
rebalancing after a delete only looked at one of the two cut nodes. The shape
test then found a third: a child left with one child could not be mended from
inside, which `mend` now prevents by filling each node before it descends.

## Against ropey

Same workload in both (`bench/bench.c`, `bench/ropey.rs`, run by
`bench/run.sh`): load the file, 50,000 random inserts and deletes of 1 to 10
bytes (logged), 50,000 lookups of each kind (line to byte, byte to line), undo
every edit, check the text came back by hash. Input: `design/proto/gen_input.py`
(ASCII Lean words, 100 MB and its first 10 MB). ropey 1.6.1, Rust 1.97 with LTO;
C with gcc 13 and clang 18 at -O2. Machine: this cloud container.

| Measure | C rope, gcc | C rope, clang | ropey | Tag |
| --- | --- | --- | --- | --- |
| Instructions in the workload, 10 MB, 5,000 edits and lookups (callgrind, `run()` only) | 54.5 M | 50.0 M | 113.7 M | measured, exact |
| Load 100 MB | median 0.087 s (0.075 to 0.104) | not run | median 0.098 s (0.090 to 0.146) | measured, 5 alternating runs: ranges overlap, no measurable difference |
| 50,000 edits on 100 MB | median 0.055 s (0.054 to 0.064) || not run | median 0.092 s (0.084 to 0.115) | measured, 5 alternating runs: C faster, ranges apart |
| 100,000 line lookups | median 0.060 s (0.057 to 0.062) || not run | median 0.092 s (0.090 to 0.115) | measured, 5 alternating runs: C faster |
| Undo of the 50,000 edits | median 0.040 s (0.039 to 0.041) || not run | median 0.054 s (0.049 to 0.061) | measured, 5 alternating runs: C faster |
| Peak RSS of the whole run (both hold the file and the read-back copy, 200 MB) | 323,008 KB | not run | 333,088 KB | measured, 1 run each |

In the C profile a third of the instructions are `nls`, the newline count
(vectorized by gcc, see the comment in `text.c`), and most of the rest is
memcpy and the descent; ropey spends most of its instructions counting chars,
UTF-16 units and lines when it builds leaves.

ropey also keeps char and UTF-16 counts, which text.h does not ask for (the
Unicode kernels in package 3 work on bytes), so part of its extra work is work
this rope does not need. Not measured: your own files (the design asks for
them; the generated corpus stands in), and leaf sizes other than ropey's.

## Proof spike

The tree is not proved. The newline counter `nls`, which every count in the
tree comes from, carries an ACSL result contract (`\result == nl(s, n)`, the
number of `'\n'` in `s[0 .. n)`), and Frama-C WP proves all 25 goals with z3 and
cvc5 (proved; `frama-c -wp -wp-rte -wp-prover z3,cvc5 -wp-timeout 20 -wp-fct nls
-cpp-extra-args=-Iinclude src/text/text.c`). A planted miscount (`|| j == 31`)
leaves 1 of 28 goals unproved (measured, 1 run). `make frama` covers only the
Unicode kernels; adding this function to it is a Makefile change for thread 0.
