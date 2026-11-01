#!/bin/bash
# A party of two discs, stage by stage (ROUND3-crossplay.md, section 12), each stage's reports under
# $OUT. Real game, hidden windows, a local server: run it where such runs are allowed.
#
#   PAL_GAME PAL_DATA PAL_LEVELS  NTSC_GAME NTSC_DATA NTSC_LEVELS   each disc's program and folders
#   MOD        the packaged mod's folder, built with -DMULTIPLAYER_BOT=ON
#   MODS_DIR   a folder of packaged mods, by id (difficulty-mod, for the enemies stage)
#   JAR JAVA   the server's jar, a JDK's java
#   PAL_SAVE NTSC_SAVE   a played save of each disc, for stage E
#   OUT        where the runs go
#
#   crossplay.sh [A B C D E F G]   the stages, all of them by default
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
: "${OUT:=runs/crossplay}"
: "${JOBS:=4}"
mkdir -p "$OUT"
common=(--mod "$MOD" --server-jar "$JAR" --java "$JAVA" --game-env SDL_VIDEODRIVER=offscreen --game-arg=--skip-movies)
pal=(--game "$PAL_GAME" --data "$PAL_DATA" --levels "$PAL_LEVELS")
ntsc=(--game "$NTSC_GAME" --data "$NTSC_DATA" --levels "$NTSC_LEVELS")
pal_member=(--member-game "$PAL_GAME" --member-data "$PAL_DATA" --member-levels "$PAL_LEVELS")
ntsc_member=(--member-game "$NTSC_GAME" --member-data "$NTSC_DATA" --member-levels "$NTSC_LEVELS")
base=8

run() {  # name, then e2e.py's arguments; at most $JOBS at once, each with its own address block
    local name=$1
    shift
    while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do wait -n; done
    timeout 420 python3 "$HERE/e2e.py" --out "$OUT/$name" --base $((base % 8 + 8)) "${common[@]}" "$@" \
        > "$OUT/$name.out" 2>&1 &
    base=$((base + 1))
    sleep 2
}

scenarios() {  # a prefix, then the host's and the members' arguments
    local tag=$1
    shift
    run "$tag-keys" --scenario keys "$@"
    run "$tag-crates" --scenario crates --crates 8 --deaths --join "$@"
    run "$tag-bolts" --scenario bolts "$@"
    run "$tag-hostdeath" --scenario hostdeath "$@"
    run "$tag-join" --scenario join --level 3 "$@"
    run "$tag-progress2" --scenario progress2 --level 3 --level2 4 "$@"
    run "$tag-soak" --scenario soak --members 3 --seconds 60 "$@"
    run "$tag-enemies" --scenario enemies --level 3 --difficulty '{"enemy_count": 2}' --mods-dir "$MODS_DIR" "$@"
}

for stage in "${@:-A B C D E F G}"; do for s in $stage; do case $s in
    A) scenarios a-ntsc "${ntsc[@]}" ;;                       # NTSC host, NTSC member
    B) scenarios b-mixed "${pal[@]}" "${ntsc_member[@]}" ;;   # PAL host, NTSC member
    C) scenarios c-mixed "${ntsc[@]}" "${pal_member[@]}" ;;   # NTSC host, PAL member
    D) # Other mods and settings still refused, and the discs alone not.
       run d-refuse-pal --scenario refuse --difficulty '{"enemy_count": 2}' --member-difficulty '{"enemy_count": 3}' \
           --mods-dir "$MODS_DIR" "${pal[@]}" "${ntsc_member[@]}"
       run d-refuse-ntsc --scenario refuse --difficulty '{"enemy_count": 2}' --member-difficulty '{"enemy_count": 3}' \
           --mods-dir "$MODS_DIR" "${ntsc[@]}" "${pal_member[@]}"
       run d-plays --scenario smoke "${pal[@]}" "${ntsc_member[@]}" ;;
    E) run e-pal-save --scenario save --save "$PAL_SAVE" "${ntsc[@]}" "${pal_member[@]}"     # the save on the other host
       run e-ntsc-save --scenario save --save "$NTSC_SAVE" "${pal[@]}" "${ntsc_member[@]}"
       run e-pal-save-host --scenario save --save "$PAL_SAVE" "${pal[@]}" "${ntsc_member[@]}"
       run e-ntsc-save-host --scenario save --save "$NTSC_SAVE" "${ntsc[@]}" "${pal_member[@]}" ;;
    F) for level in 7 12 13 15 17 18; do                      # the levels whose data differs
           run "f-join-$level" --scenario join --level "$level" "${pal[@]}" "${ntsc_member[@]}"
       done ;;
    G) run g-scene --scenario join --level 3 --host-scene 2 "${ntsc[@]}" "${pal_member[@]}"
       run g-scene-rev --scenario join --level 3 --host-scene 2 "${pal[@]}" "${ntsc_member[@]}" ;;
    *) echo "no stage $s" >&2; exit 2 ;;
esac; done; done
wait

python3 - "$OUT" <<'PY'
import json, pathlib, sys
failed = 0
for path in sorted(pathlib.Path(sys.argv[1]).glob("*/report.json")):
    report = json.loads(path.read_text())
    bad = [c for c in report["checks"] if not c["ok"]]
    failed += 0 if report["passed"] else 1
    print(f"{'pass' if report['passed'] else 'FAIL'} {path.parent.name}" + "".join(f"\n     {c}" for c in bad[:4]))
print("failed:", failed)
sys.exit(1 if failed else 0)
PY
