#!/usr/bin/env bash
# Run Aestra the way the 4 GB target laptop (i5-3337U, 2 cores) would feel it.
#
# Caps ONLY Aestra's process tree with a systemd cgroup; the rest of the PC keeps its RAM.
#   - hard memory cap, soft limit below it (throttling you can feel before the kill)
#   - no swap/zram for the scope: over the cap it fails fast instead of thrashing
#   - two CPUs, like the target
# Memory only: the GPU (HD 4000 fill rate) and CPU speed are not emulated, so use this for memory
# behaviour and for before/after comparisons, not as the final word on frame time.
#
# Usage: scripts/aestra-4gb.sh [options] [-- aestra args]
#   --binary PATH   Aestra binary (default: build-linux/bin/Aestra)
#   --max MB        hard cap in MB (default 3500, leaves ~500 MB for the OS on a 4 GB machine)
#   --high MB       soft limit in MB (default 3000). Past it the kernel throttles hard, so a runaway
#                   can crawl for a long time before the cap kills it; --high = --max kills sooner
#   --cpus LIST     allowed CPUs (default 0-1)
#   --stats         set AESTRA_FRAME_STATS=1 (frame WORK time, SPEC 3 section 4)
#   --swap          allow swap (default: none)
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$repo/build-linux/bin/Aestra"
max=3500 high=3000 cpus="0-1" swap=0 stats=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) binary="$2"; shift 2 ;;
    --max)    max="$2"; shift 2 ;;
    --high)   high="$2"; shift 2 ;;
    --cpus)   cpus="$2"; shift 2 ;;
    --stats)  stats=1; shift ;;
    --swap)   swap=1; shift ;;
    --)       shift; break ;;
    -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "aestra-4gb: unknown option $1 (see --help)" >&2; exit 2 ;;
  esac
done

[[ -x "$binary" ]] || { echo "aestra-4gb: $binary not found or not executable (use --binary)" >&2; exit 1; }
command -v systemd-run >/dev/null || { echo "aestra-4gb: needs systemd-run" >&2; exit 1; }
(( high <= max )) || { echo "aestra-4gb: --high ($high) must not exceed --max ($max)" >&2; exit 2; }

# AllowedCPUs needs the cpuset controller, which user services are not given (it is silently ignored),
# so pin with taskset and bound the time with CPUQuota instead.
command -v taskset >/dev/null || { echo "aestra-4gb: needs taskset (util-linux)" >&2; exit 1; }
ncpu="$(taskset -c "$cpus" nproc 2>/dev/null)" || { echo "aestra-4gb: bad --cpus '$cpus'" >&2; exit 2; }
props=(-p "MemoryMax=${max}M" -p "MemoryHigh=${high}M" -p "CPUQuota=$((ncpu * 100))%")
(( swap )) || props+=(-p "MemorySwapMax=0")
env_args=()
(( stats )) && env_args+=(--setenv=AESTRA_FRAME_STATS=1)

unit="aestra-4gb-$$"
echo "aestra-4gb: cap ${max} MB (soft ${high} MB), cpus ${cpus}, swap $( (( swap )) && echo on || echo off ) -- unit ${unit}" >&2

# The scope's accounting disappears when Aestra exits, so sample its peak while it runs.
peakfile="$(mktemp)"
trap 'rm -f "$peakfile"' EXIT
(
  cg="/sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/app.slice/${unit}.scope"
  while sleep 1; do
    [[ -r "$cg/memory.peak" ]] && cat "$cg/memory.peak" > "$peakfile" 2>/dev/null
  done
) &
sampler=$!
set +e
systemd-run --user --scope --quiet --unit="$unit" "${props[@]}" "${env_args[@]}" taskset -c "$cpus" "$binary" "$@"
status=$?
set -e
kill "$sampler" 2>/dev/null || true
peak="$(cat "$peakfile" 2>/dev/null || true)"
if [[ "$peak" =~ ^[0-9]+$ ]]; then
  echo "aestra-4gb: peak memory ~$((peak / 1024 / 1024)) MB (cap ${max} MB), exit status ${status}" >&2
else
  echo "aestra-4gb: exit status ${status} (no peak sample: Aestra ran under a second)" >&2
fi
(( status == 137 )) && echo "aestra-4gb: killed (137): Aestra went over the ${max} MB cap" >&2
exit "$status"
