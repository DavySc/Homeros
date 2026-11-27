#!/usr/bin/env bash
# spin.sh K SECONDS: K busy loops; half of them walk 64 MB so the caches and
# memory bus are busy too
for i in $(seq $1); do
  if (( i % 2 )); then timeout $2 sh -c 'while :; do :; done' &
  else timeout $2 python3 -c "
b = bytearray(64 << 20)
while True:
    for i in range(0, len(b), 4096): b[i] = (b[i] + 1) & 255
" & fi
done; wait
