#!/bin/bash
# sweep.sh TAG OUT E2E_ARGS...: the crates scenario at every loss and latency, 4 runs at once, into
# OUT/TAG-l<loss>-m<ms>; E2E_ARGS as e2e.py's (--game, --data, ...; not --out, --base, --link).
# The latency is the round trip to the server. report.py makes the tables.
HERE=$(cd "$(dirname "$0")" && pwd)
TAG=$1; OUT=$2; shift 2
b=0
for loss in 0 1 5 15; do for ms in 30 100 250; do
  while [ "$(jobs -r | wc -l)" -ge 4 ]; do wait -n; done
  n=$TAG-l$loss-m$ms
  link="loss=$loss,latency=$((ms/2)),jitter=$((ms/5)),reorder=2,duplicate=1"
  timeout 320 python3 "$HERE/e2e.py" --out "$OUT/$n" --base $((b%4+4)) --scenario crates --crates 8 --deaths --join \
    --link "$link" --game-arg=--skip-movies "$@" > "$OUT/$n.out" 2>&1 &
  b=$((b+1))
  sleep 2
done; done
wait
