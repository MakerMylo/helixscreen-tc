#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PreToolUse hook on Bash (wired in .claude/settings.json). Suggests where a
# heavy command should run; it never blocks and never decides permission. The
# answer is a context block the model reads before the command runs, or nothing.
#
# thelio is shared by several sessions at once; zeus has twice the RAM and is
# usually idle. Some work belongs on zeus however quiet thelio looks (gates,
# mutation, sanitizers, symbolizers); the rest is only worth moving when thelio
# is tight. "Tight" is not decided here: `helix-claim jobs -v` already folds in
# claimed builds, live build trees and MemAvailable, so this reads its share and
# its availGB and applies one threshold to each.
#
# Runs on EVERY Bash call, so a command matching no heavy pattern returns before
# touching /proc, helix-claim or jq. Never ssh from here.
#
# Env:
#   HELIX_ADVISOR_JOBS_CMD    command printing the `jobs -v` line (default: helix-claim jobs)
#   HELIX_ADVISOR_MIN_SHARE   share at or below which thelio is tight (default 8)
#   HELIX_ADVISOR_MIN_GB      availGB below which thelio is tight (default 16)
#   HELIX_ADVISOR_ZEUS_RUN    zeus-run.sh whose modes are offered (default: beside this script)

input=$(cat)

# Fast path: a plain substring test on the raw JSON, no parsing.
case "$input" in
    *addr2line*|*gdb*|*llvm-symbolizer*|*full-test-run*|*unit-sweep*|*mutate*|*asan*|*-docker*|*idf*|*" -j"*|*helix-tests*|*bats*) ;;
    *) exit 0 ;;
esac

command -v jq >/dev/null 2>&1 || exit 0
cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty' 2>/dev/null) || exit 0
[ -n "$cmd" ] || exit 0

# Already headed for zeus.
case "$cmd" in
    *zeus-run.sh*|*"ssh zeus"*) exit 0 ;;
esac

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
zeus_run=${HELIX_ADVISOR_ZEUS_RUN:-"$here/zeus-run.sh"}

emit() {
    jq -cn --arg c "[resource-advisor] $1" \
        '{hookSpecificOutput: {hookEventName: "PreToolUse", additionalContext: $c}}'
    exit 0
}

# The zeus-run mode for a full sweep, if this checkout's zeus-run has one.
zeus_gate_mode() {
    local m
    for m in full sweep; do
        grep -qE "^[[:space:]]*${m}\)" "$zeus_run" 2>/dev/null && { echo "$m"; return; }
    done
}

push_then="zeus runs only pushed SHAs: push the branch (never main), then"

# Patterns live in variables: `;|&(` inside a literal [[ =~ ]] break the parse.
re_gdb='(^|[[:space:];|&(])gdb[[:space:]].*helix-(tests|screen)'
re_symbolizer='(^|[[:space:];|&(])(addr2line|llvm-symbolizer)[[:space:]]'
re_mutate='mutate(-diff|_diff)'
re_sweep='make[[:space:]].*(full-test-run|unit-sweep)'
re_asan='(^|[[:space:]])make[[:space:]].*asan'
re_make_j='(^|[[:space:]])make[[:space:]](.*[[:space:]])?-j([0-9]+)'
re_container='make[[:space:]][^|;]*-docker|docker[[:space:]]+run.*idf'
re_loop='(for|while)[[:space:]].*(helix-tests|bats)'

# --- Always on zeus --------------------------------------------------------

if [[ "$cmd" =~ $re_gdb ]]; then
    emit "gdb against a helix binary takes ~35 min and tens of GB on thelio. \`coredumpctl info <pid>\` symbolizes a core in seconds; anything more goes to zeus (${push_then} build there and run gdb -batch)."
fi
if [[ "$cmd" =~ $re_symbolizer ]] && [[ "$cmd" == *helix-* ]]; then
    emit "addr2line loads the whole DWARF of helix-tests per call (21GB+ RSS on thelio) and ignores SIGTERM. Symbolize on zeus: ${push_then} build in the helix-tsan container and use llvm-symbolizer or one \`gdb -batch -ex 'info symbol 0x..'\` there. For a function name alone, \`nm -C --defined-only\` plus a sorted lookup is seconds."
fi
if [[ "$cmd" =~ $re_mutate ]]; then
    emit "Mutation rebuilds and reruns the suite per hunk. Run it on zeus: ${push_then} \`scripts/zeus-run.sh mutate --tests '[tag]'\`."
fi
if [[ "$cmd" =~ $re_sweep ]]; then
    mode=$(zeus_gate_mode)
    if [ -n "$mode" ]; then
        emit "A full sweep starts dozens of shards at once. Run it on zeus: ${push_then} \`scripts/zeus-run.sh ${mode}\`. Keep it local only when you need the verdict on uncommitted work in this exact tree."
    fi
    emit "A full sweep starts dozens of shards at once. zeus is the place for it once \`scripts/zeus-run.sh\` has a sweep mode (\`zeus-run test\` is serial and not a gate). Until then, check \`scripts/helix-claim jobs -v\` first and run it when peers are not sweeping."
fi
if [[ "$cmd" =~ $re_asan ]]; then
    emit "ASAN produces no output on thelio (ld.so.preload loads its runtime second) and exits 0. Run it on zeus: ${push_then} \`scripts/zeus-run.sh asan '[tag]'\`."
fi

# --- Only when thelio is tight ---------------------------------------------

# Reading the box costs a pgrep sweep; skip it when nothing below could fire.
[[ "$cmd" =~ $re_make_j || "$cmd" =~ $re_container || "$cmd" =~ $re_loop ]] || exit 0

jobs_cmd=${HELIX_ADVISOR_JOBS_CMD:-"$here/helix-claim jobs"}
line=$($jobs_cmd -v 2>&1 >/dev/null) || exit 0
share=$(printf '%s' "$line" | sed -n 's/.*-> -j\([0-9][0-9]*\).*/\1/p')
avail=$(printf '%s' "$line" | sed -n 's/.*availGB=\([0-9][0-9]*\).*/\1/p')
[ -n "$share" ] && [ -n "$avail" ] || exit 0

min_share=${HELIX_ADVISOR_MIN_SHARE:-8}
min_gb=${HELIX_ADVISOR_MIN_GB:-16}
tight=""
if [ "$avail" -lt "$min_gb" ]; then
    tight="thelio has ${avail}GB available"
elif [ "$share" -le "$min_share" ]; then
    tight="thelio's fair share is -j${share}"
fi
state="${tight:-thelio is not tight}; \`scripts/helix-claim jobs -v\` shows the detail"

if [[ "$cmd" =~ $re_make_j ]]; then
    asked=${BASH_REMATCH[3]}
    if [ "$asked" -gt "$share" ] || [ -n "$tight" ]; then
        if [ "$asked" -gt "$share" ]; then
            emit "-j${asked} is above the fair share (-j${share}); ${state}. Use \`-j\$(scripts/helix-claim jobs)\` and take \`scripts/helix-claim take build:<tree>\` first."
        fi
    fi
fi

[ -n "$tight" ] || exit 0

if [[ "$cmd" =~ $re_container ]]; then
    emit "Container builds escape thelio's -j and nice, and ${state}. Run it on zeus (it has the Docker images and twice the RAM), or wait for peers' builds to finish."
fi
if [[ "$cmd" =~ $re_loop ]]; then
    emit "A loop over the test binary multiplies its load, and ${state}. Cut the count, or run the loop on zeus: ${push_then} use the helix-tsan container."
fi

exit 0
