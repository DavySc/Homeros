# pstack skills trial

Run 2026-11-17, as DESIGN.md's "pstack as the project harness", Proposal (revised after review), asks: five small package 3 tasks done with the four skills and without them, compared on defects found by a Fable review, lines of code and tokens; keep the skills only if the difference is outside the run-to-run spread.

**Verdict: do not keep the skills.** Defects and lines of code show no measurable difference (each pair's gap is inside the spread between pairs, and the sign goes both ways). Tokens went up with the skills in all 5 pairs (by 1 % to 29 %, median 16 %), so they cost something and bought nothing measurable. Separately, all four skills are marked `disable-model-invocation: true`, so in Claude Code they never run unless someone types the slash command; copied into `.claude/skills` they would do nothing on their own. **measured** (10 attempts, 10 reviews, counts below)

## The skills

benchmark-checklist, create-formal-verification-skill, maintain-formal-verification-skill and tdd, copied unchanged from pstack 0.15.43 (github.com/cursor/plugins, `pstack/`, commit 2f87c854fe3e2fcda44ca86b46ede35aad3c46b7 of 2026-11-16, MIT, Lauren Tan): 206 lines with the licence. They are not added to this repository, since the verdict is not to keep them.

## Method

- **Base.** `origin/main` 9817eb8 merged with PR 12's head 2373e75 (package 3, Unicode kernels), so that every task starts from a building tree with the whole package 3 present. For each task one piece of PR 12 was cut out (`skills-trial/surgery.py`), with the checks aimed at it. PR 12's own version of that piece is the hidden reference.
- **Tasks** (full text in `skills-trial/tasks.json`):
  1. T1, `uni_gprev`: implement against the fixed contract, Frama-C proves it, add tests, fuzz properties and plants.
  2. T2, `uni_width`: the same for Neovim's `utf_ptr2cells()`, tested against Neovim on every code point.
  3. T3, a new pair of UTF-16 column kernels for LSP, as Neovim's `vim.str_utfindex` / `str_byteindex`, with contracts written by the agent (no reference exists).
  4. T4, `src/uni/gen.py` and the `tab.h` it makes, from the Unicode 18 UCD.
  5. T5, `tests/test_uni_nvim.c`: check the tables, `uni_glen` and `uni_width` against Neovim, with plants.
- **Pairs.** Each task was done twice: once with `.claude/skills/` in the repo and the prompt line "Read each SKILL.md before you start, and follow each one wherever it applies to this task", once without either. Prompts were otherwise identical. Because the skills cannot be invoked by the model, telling the agent to read them is the most generous way to give them a chance. The two attempts of a pair ran at the same time, so both met the same machine load; this replaces the "alternating" order (with simultaneous runs there is no order).
- **Independence.** Each attempt was a fresh subagent (Opus) in its own git repository holding only the base commit, with no history, no other branches and no other attempt's output, told to work only there and not to use GitHub. Each review was a fresh Fable 5.2 subagent (`claude-fable-5-2`) in a new repository holding the task's base without skills plus the attempt's diff with `.claude/` removed, so it could not tell the arm. It got the task text and CLAUDE.md, not the author's report, and had to reproduce every defect by running it; unreproduced suspicions did not count. Leak check: of the attempts' added lines longer than 25 characters, 1 to 7 per attempt also occur in PR 12's files, all generic idioms (`size_t k = uni_len(s, n, i);`, `if __name__ == '__main__':`). **measured**
- **Labels.** The ten attempts got random ids (`skills-trial/key.json`). The run and review scripts are in `skills-trial/`; the reviews and the diffs (without the generated `tab.h`) are `skills-trial/<id>-review.md` and `<id>.diff`.
- **Hidden check.** After all reviews, each attempt was run alone against PR 12's own checks: for T1 and T2 PR 12's `test_uni_nvim.c` and `fuzz_uni.c` (60 s) replace the attempt's, plus `make frama` (uncached, on a quiet machine where the cached result was a timeout); for T3 and T4 compilers, frama and tests, and for T4 `gen.py` was rerun on freshly downloaded UCD 18.0.0 files; for T5 PR 12's three `uni_nvim` plants were planted under the attempt's test.

## Results

| Task | Arm | Id | Major | Minor | Lines added (src+include / tests) | Tokens | Tool calls | Wall time |
|---|---|---|---|---|---|---|---|---|
| T1 gprev | with | r28 | 0 | 2 | 33 (19 / 14) | 112,626 | 52 | 41 min |
| T1 gprev | without | r13 | 0 | 2 | 42 (24 / 18) | 97,220 | 36 | 69 min |
| T2 width | with | r39 | 2 (1 forced) | 0 | 50 (13 / 37) | 121,332 | 38 | 49 min |
| T2 width | without | r35 | 1 (1 forced) | 1 | 51 (17 / 34) | 99,909 | 35 | 50 min |
| T3 UTF-16 | with | r37 | 0 | 3 | 317 (125 / 192) | 149,486 | 41 | 62 min |
| T3 UTF-16 | without | r53 | 0 | 4 | 283 (126 / 157) | 144,431 | 53 | 129 min |
| T4 tables | with | r48 | 0 | 2 | 99 (99 / 0) | 141,550 | 59 | 68 min |
| T4 tables | without | r54 | 0 | 1 | 127 (126 / 1) | 139,619 | 71 | 124 min |
| T5 Neovim test | with | r69 | 1 | 4 | 230 (0 / 230) | 166,997 | 49 | 50 min |
| T5 Neovim test | without | r17 | 1 | 6 | 232 (0 / 232) | 129,647 | 50 | 69 min |

Tokens, tool calls and wall time are as the harness reported them at each subagent's end. "Forced": both T2 reviewers found that `uni_width` differs from Neovim on 5- and 6-byte forms, which the fixed contract in `uni.h` requires, so the author could not avoid it (see Side findings). Lines exclude the generated `tab.h`; both T4 tables are 32,896 bytes of data, the same as PR 12's.

Hidden check: all 10 pass. T1 and T2: PR 12's tests and fuzzer pass on every attempt, and `make frama` proves every goal (r28 225 / 225, r39 227 / 227 uncached on a quiet machine). T3: both pass frama and tests. T4: both `gen.py` regenerate their committed `tab.h` byte for byte (and PR 12's does the same for its own), and PR 12's Neovim test passes. T5: both tests catch all three of PR 12's plants. **measured** (1 run each)

### Per pair, with minus without

| Task | Defects (all) | Majors | Lines added | Tokens |
|---|---|---|---|---|
| T1 | 0 | 0 | −9 | +15,406 (+16 %) |
| T2 | 0 | +1 | −1 | +21,423 (+21 %) |
| T3 | −1 | 0 | +34 | +5,055 (+3 %) |
| T4 | +1 | 0 | −28 | +1,931 (+1 %) |
| T5 | −2 | 0 | −2 | +37,350 (+29 %) |
| total | 14 against 16 | 3 against 2 | | 692.0k against 610.8k |

- **Defects:** pair gaps from −2 to +1, median 0, two pairs tied; with fewer in 2 pairs and more in 1. The gaps go both ways and are no larger than the spread between tasks, so: no measurable difference. **measured** (5 pairs)
- **Lines:** gaps from −28 to +34, median −2, both signs: no measurable difference. **measured** (5 pairs)
- **Tokens:** higher with the skills in 5 of 5 pairs (median +16 %, range +1 % to +29 %). Reading the skills is about 3,000 tokens, which explains T3 and T4 but not the rest. Five of five in one direction has a chance of 1 in 16 if the arms were equal (two-sided), so this is the one result that leans one way, and it is a cost. **measured** (5 pairs)
- **Wall time:** shorter with the skills in 5 of 5 pairs, but this is not evidence: the arms shared a loaded machine (load average 5 to 14 on 4 cores), several runs waited on background jobs, and one review's `pkill` may have killed a Frama-C run of r53. Not used in the verdict. **hypothesis**

### What the skills changed, by the authors' own reports

Three of the five "with" authors said tdd applied (a failing test or link error before the code); the "without" authors worked the same way where it was natural (r53 probed Neovim before writing its kernels, r54 dumped Neovim's values before writing gen.py). Every "with" author said benchmark-checklist did not apply except r28, which measured `uni_gprev`'s cost (5 runs per size) unasked. No author generated a verify skill: the create/maintain formal-verification skills had nothing to do, since the project already has Frama-C, plants and `make planted`, which is what those skills would create.

One difference went the other way: three of the five "without" authors (r13, r35, r54) found that a base Frama-C goal in `uni_glen` sits at the 20 s limit and fixed it with one prover assertion; no "with" author did. With 5 pairs this is chance as far as the trial can tell. **hypothesis**

## Residual risk

- Five pairs on one package. A small effect, or an effect on other kinds of work (benchmarks, bug fixes, new checks in a package without plants), would not show here.
- One reviewer per attempt; reviewers vary (T5's two reviewers found 5 and 7 defects in near-identical work). Pairing by task controls the task, not the reviewer.
- The "with" arm was told to read the skills, which is more than the skills get in normal use (they never trigger by themselves), so the trial overstates their effect if anything.
- Isolation was by instruction, not enforced: the agents could have read other directories. The leak check found nothing, and one author wrote a scratch benchmark outside its repository.

## Side findings for package 3 (PR 12), each found independently by two or more agents

1. **5- and 6-byte forms.** Neovim 0.12 reads a lead byte F8..FD with its continuation bytes as one character (`utf8len_tab`); `uni.h`'s contract reads it as an invalid byte. `f8 88 80 80 80` is one 5-byte cluster of 1 cell in Neovim and five clusters of 4 cells here. Found by both T2 reviews, both T5 authors and the T3 authors (who copied Neovim in their new kernels). Needs a decision by the `uni.h` owner. **measured**
2. **CR before an overlong LF.** `0d c0 8a` is one 3-byte cluster in Neovim (GB3) and 1 + 2 here, because the table merges CR, LF and Control. Found by r17's review; none of the Neovim tests, PR 12's included, draws that pair. **measured**
3. **A fragile proof.** `typed_uni_glen_loop_invariant_3_preserved` needs 17 to 20 s of Qed time against the 20 s limit, so `make frama` fails under load, and Frama-C's cache then replays the timeout. One assertion in the loop, for example `//@ assert t == gcb(c) && it == incb(c);` (r54), brings the slowest goal to 0.77 s. **measured** (r54: 2 runs; r35 and r13 with other assertions)
4. **Step back on long lines.** `uni_gprev` (PR 12's and all attempts') rescans from the last ASCII byte, so walking left across a 30,000-byte CJK line with no ASCII takes about 1.1 s. **measured** (r28's review, 5 runs)
5. The regional-indicator branch of `uni_width`'s VS16 rule never changes a result on Unicode 18 data (all 26 are already wide), so only Frama-C can see it. **measured**
