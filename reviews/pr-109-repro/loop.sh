# loop.sh DIR N TAG: run DIR's test_infoview N times, log each verdict
cd "$1"; for i in $(seq "$2"); do
  if ./build/san/test_infoview > /tmp/claude-0/loops/$3.$i.log 2>&1; then echo "$i pass"; else echo "$i FAIL $(grep -m1 'failed' /tmp/claude-0/loops/$3.$i.log)"; fi
done > /tmp/claude-0/loops/$3.txt; echo end >> /tmp/claude-0/loops/$3.txt
