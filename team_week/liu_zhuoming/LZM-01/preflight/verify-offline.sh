#!/usr/bin/env bash
set -euo pipefail
umask 077
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
[[ $EUID == 0 ]] || { echo 'Run as root in WSL; checker runs as omm.' >&2; exit 2; }

run=$(mktemp -d /tmp/fides-preflight.XXXXXXXX)
chmod 0711 "$run"
install -d -m 0700 -o omm -g dbgrp "$run/fixtures"
echo "OFFLINE_EVIDENCE=$run"

as_omm() {
  runuser -u omm -- bash -c 'source /home/omm/.sse_opengauss_env; exec "$@"' _ "$@"
}

snapshot() {
  (
    cd "$run/fixtures"
    LC_ALL=C find . -printf '%y %m %u:%g %P\n' | LC_ALL=C sort
    find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum
  )
}

as_omm python3 "$here/generate_fides_fixtures.py" "$run/fixtures"
snapshot > "$run/before"
as_omm python3 "$here/verify_cases.py" "$run/fixtures" > "$run/executed"
as_omm python3 -c '
import json
import sys

for line in sys.stdin:
    if line.startswith("PASS:"):
        print(line.rstrip())
        continue
    case = json.loads(line)
    print("PASS {case}: {fixture}, exit={exit}".format(**case))
' < "$run/executed"

status=0
as_omm python3 "$here/check_fides_state.py" \
  --counter-file "$run/fixtures/normal/fides_client.mat" \
  --mapping-file "$run/fixtures/normal/sse_table.mat" --table fides_demo \
  > "$run/argument.stdout" 2> "$run/argument.stderr" || status=$?
[[ $status == 2 && ! -s $run/argument.stdout ]]
grep -q -- '--keyword' "$run/argument.stderr"
cat "$run/argument.stderr"

snapshot > "$run/after"
cmp "$run/before" "$run/after"
echo 'PASS OFF-01: missing --keyword, exit=2; stdout empty; fixture tree unchanged'
