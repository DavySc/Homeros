t="nightly: fresh-seed differences"
body=$(printf 'Seed %s, run %s/%s/actions/runs/%s, commit %s\n\n```\n%s\n```\n' \
  "44220282885" "$GITHUB_SERVER_URL" "$GITHUB_REPOSITORY" "44220282885" "$GITHUB_SHA" \
  "$(grep -E 'differ|DIFF|STALE|FAIL|Error' nightly.log | tail -40)")
n=$(gh issue list --state open --search "\"$t\" in:title" --json number --jq '.[0].number')
if [ -n "$n" ]; then gh issue comment "$n" --body "$body"; else gh issue create --title "$t" --body "$body"; fi
