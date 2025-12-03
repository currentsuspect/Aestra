# Architecture Migration Guide

## Overview

The `src/` directory is the new home for Nomad DAW's architecture. This directory contains the NEW architecture structure.

It uses facade/adapter patterns to wrap existing code from:

* NomadCore/   → src/core/
* NomadAudio/  → src/audio/, src/dsp/
* NomadUI/     → src/ui/
* Source/      → src/app/

## Migration Strategy

## Phase 1: Header Migration

1. New headers in src/ include existing headers from Nomad*/
2. Re-export in new namespace structure (nomad::core, nomad::audio, etc.)
3. Add any missing interfaces/abstractions
4. Gradually move implementation into src/ as we refactor
5. Eventually deprecate Nomad*/ directories

DO NOT duplicate functionality - wrap and migrate!
