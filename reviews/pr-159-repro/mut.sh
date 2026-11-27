#!/bin/bash
# mut.sh: the reviewer's own bugs in tds.c's cancel path, each against
# cancel_tokens() alone (tests/iso.c, no server): which does the new case see?
T=/tmp/claude-0/rv/tree-h0
cd $T
grep -rl "match_num" src/*/*.c | head -3 >/dev/null
DEPS="src/sql/net.c src/sql/pg.c src/date/*.c src/str/*.c src/uni/*.c $(grep -l '^long match_num\|match_num(const' src/*/*.c ref/*.c 2>/dev/null | grep -v sql | head -1)"
cp src/sql/tds.c /tmp/claude-0/rv/tds.orig
while IFS='~' read -r name old new; do
  [ -z "$name" ] && continue
  python3 - "$old" "$new" <<'PY' || { echo "BROKEN  $name"; continue; }
import sys
s=open('/tmp/claude-0/rv/tds.orig').read()
if sys.argv[1] and s.count(sys.argv[1])!=1: sys.exit(1)
open('/tmp/claude-0/rv/tds.mut.c','w').write(s.replace(sys.argv[1],sys.argv[2]) if sys.argv[1] else s)
PY
  cp /tmp/claude-0/rv/tds.mut.c src/sql/tds.c
  if ! gcc -std=c17 -D_DEFAULT_SOURCE -Iinclude -fsanitize=address,undefined -g -o /tmp/claude-0/rv/iso-min tests/iso.c src/sql/tds.c $DEPS $(pkg-config --libs openssl) 2>/tmp/claude-0/rv/mut.err; then echo "NO BUILD  $name: $(grep -m1 -E 'error|undefined' /tmp/claude-0/rv/mut.err)"; continue; fi
  out=$(/tmp/claude-0/rv/iso-min 2>&1 | head -1)
  echo "$name => $out"
done <<'EOF'
clean~~
own plant 1 (firstdone)~(t->state == TDS_CANCEL && st & 0x20)~t->state == TDS_CANCEL
own plant 2 (3980 shown)~if (t->quiet && no == 3980) return 0;~if (t->quiet && no == 1) return 0;
3980 line removed~		if (t->quiet && no == 3980) return 0;   ~		//
attention bit read as 0x10~t->state == TDS_CANCEL && st & 0x20~t->state == TDS_CANCEL && st & 0x10
attention bit ignored (never ready)~(t->state == TDS_CANCEL && st & 0x20)~0
DONE's "more" bit ignored~if (k != 0xFF && !(st & 1) && (t->state == TDS_BUSY~if (k != 0xFF && (t->state == TDS_BUSY
3980 number read 2 bytes~unsigned no = (unsigned)num(&m, 4), state~unsigned no = (unsigned)num(&m, 2) + 0 * (unsigned)num(&m, 2), state
3980 hides every quiet error~if (t->quiet && no == 3980) return 0;~if (t->quiet) return 0;
3980 hidden when not cancelling~if (t->quiet && no == 3980) return 0;~if (no == 3980) return 0;
quiet kept after the cancel ends~t->state = TDS_READY, t->quiet = 0;~t->state = TDS_READY;
a quiet rows-affected line shown~if (st & 0x10 && !t->quiet && str_addf~if (st & 0x10 && str_addf
3980 dropped as a broken token~if (t->quiet && no == 3980) return 0;~if (t->quiet && no == 3980) return -1;
EOF
cp /tmp/claude-0/rv/tds.orig src/sql/tds.c
