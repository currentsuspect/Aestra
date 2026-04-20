#!/usr/bin/env bash
# Resonance Pro launcher for Aestra development
# Loads OpenCode Zen credentials and launches the Aestra Executor agent

set -euo pipefail

HARNESS_DIR="$HOME/Dev/resonance-code"
AESTRA_DIR="$HOME/Dev/Aestra"
WORK_DIR="$(pwd)"

if [ ! -d "$HARNESS_DIR" ]; then
  echo "Error: Resonance Pro harness not found at $HARNESS_DIR"
  exit 1
fi

# OpenCode Zen credentials (MiniMax M2.5 Free)
export CLAUDE_CODE_USE_OPENAI=1
export RESONANCE_USE_OPENAI=1
export RESONANCE_OPENAI_BASE_URL=https://opencode.ai/zen/v1
export RESONANCE_OPENAI_MODEL=minimax-m2.5-free
export RESONANCE_OPENAI_API_KEY=sk-qEE8jCzhug9bqQiS972UvEWcmzmz5hLbTv1Rmh3JbaDunA62arYiZqiVgcw3dfZx

# Add Aestra scripts to PATH so the agent can find them
export PATH="$AESTRA_DIR/scripts:$PATH"

# Run from the current directory (Aestra), not the harness
cd "$WORK_DIR"
exec bun "$HARNESS_DIR/scripts/dev.ts" --agent aestra-executor --dangerously-skip-permissions "$@"
