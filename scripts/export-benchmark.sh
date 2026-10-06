#!/usr/bin/env bash
# Re-run the export benchmark from docs/PERFORMANCE.md §10 on this machine.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${LECTERN_BUILD:-$ROOT/build/dev}"
EXPORT="$BUILD/bin/lectern-export"
REC="$BUILD/bin/lectern-rec"
OUT="$ROOT/docs/benchmarks/export-$(date +%Y%m%d).log"
mkdir -p "$ROOT/docs/benchmarks"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$REC" --synthetic --duration 20 --out "$WORK/proj" --title "Perf benchmark" >/dev/null
PROJECT=$(find "$WORK/proj" -name '*.lectern' -type d | head -1)
OUTFILE="$WORK/out.mp4"

/usr/bin/time -l "$EXPORT" "$PROJECT" --out "$OUTFILE" --resolution 1080p --fps 30 2>"$WORK/time.txt" | tee "$OUTFILE.export.log"

{
  echo "# Export benchmark $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "Machine: $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)"
  echo "Build: $BUILD"
  cat "$WORK/time.txt"
} | tee "$OUT"

echo "Logged to $OUT"
