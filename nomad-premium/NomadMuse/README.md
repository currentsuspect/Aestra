# NomadMuse - Integrated AI Music Production Assistant

## Overview
NomadMuse is a comprehensive AI music production assistant for the NOMAD DAW, integrating three specialized neural modules:
1. **MIDI Generation** (300k params) - Real-time symbolic music prediction
2. **Audio Classification** (150k params) - Instrument recognition and track-type detection  
3. **Spectral Analysis** (DSP + 10k params) - Mixing and mastering guidance

All processing happens locally with <20ms latency, ensuring privacy and eliminating cloud dependencies.

## 🎯 Status
✅ **Research Complete** - Full technical specification documented  
⚠️ **In Development** - Week 1 of 40-week implementation roadmap  
⏳ **Dataset Acquisition** - GigaMIDI, NSynth, E-GMD, Mixing Secrets needed

## 🚀 Key Features

### Three-Module Architecture
- **Context-Aware Generation:** Understands drums vs. melodic instruments
- **Real-Time Classification:** Identifies what you're playing/recording  
- **Production Guidance:** Professional mixing suggestions based on genre

### Privacy-First Design
- All data stays local by default
- Optional anonymized telemetry (opt-in)
- Personal adapters learn from your style

### Breakthrough Performance
- < 20ms end-to-end latency (imperceptible delay)
- > 80% user acceptance rate target
- < 50 MB memory footprint

## 📚 Research Documentation

### Complete Technical Specifications
1. **[NOMAD_MUSE_RESEARCH_PAPER.md](research/NOMAD_MUSE_RESEARCH_PAPER.md)** ⭐ **MAIN DOCUMENT**
   - Complete three-module architecture
   - Detailed training procedures with code
   - Cross-modal fusion and communication
   - 40-week implementation roadmap
   
2. **[INTEGRATED_VISION_EXTENDED.md](research/INTEGRATED_VISION_EXTENDED.md)**
   - Extended vision and use cases
   - Dataset acquisition strategies
   - Multi-modal integration details

## 📁 Directory Structure
```
NomadMuse/
├── README.md                           # This file
├── CMakeLists.txt                      # Build configuration
├── include/                            # Public API headers (future)
├── src/                                # Implementation files (future)
├── models/                             # Trained AI models
│   ├── midi_gen_int8.onnx             # MIDI generator (300k params)
│   ├── audio_classifier_int8.onnx     # Audio classifier (150k params)
│   └── genre_curves/                  # Spectral analysis targets
│       ├── pop.npy
│       ├── rock.npy
│       └── ... (8 genres)
├── research/                           # Research papers
│   ├── NOMAD_MUSE_RESEARCH_PAPER.md   # ⭐ Main technical specification
│   └── INTEGRATED_VISION_EXTENDED.md  # Extended vision document
└── test/                               # Unit tests (future)
```

## 🎓 Research Summary

### Module 1: MIDI Generation
- **Architecture:** GRU/Transformer-lite hybrid (300k parameters)
- **Training:** 4-stage distillation (Base 2.5M → Student 200k → Personal 50k → Fusion 300k)
- **Datasets:** GigaMIDI (1.4M files), MAESTRO, Groove MIDI, Lakh Clean
- **Modes:** Dual-mode generation (Tonal for melodic, Percussive for drums)
- **Latency:** < 20ms target

### Module 2: Audio Classification  
- **Architecture:** Lightweight CNN (150k parameters)
- **Output:** 20 instrument classes + percussive/tonal detection
- **Datasets:** NSynth (305k notes), Groove Audio, E-GMD, Mixing Secrets
- **Accuracy:** > 90% instrument recognition, > 95% drum detection
- **Latency:** < 10ms target

### Module 3: Spectral Analysis
- **Architecture:** DSP-based FFT + rule engine (10k parameters)
- **Features:** 7-band frequency analysis, 8 genre target curves
- **Datasets:** 20,000+ reference mixes across genres
- **Output:** Prioritized mixing suggestions with EQ recommendations
- **Update Rate:** 1-2 seconds (soft real-time)

### Cross-Modal Fusion
- **Unified State Manager:** Thread-safe global context
- **Bidirectional Flow:** Classifier → Generator (track type), Generator → Analyzer (what's playing)
- **Lock-Free Communication:** Ring buffers, atomic operations
- **Learning:** RLHF from user feedback, personal adaptation

## 🛠️ Getting Started

### Prerequisites (for development)
- C++17 compiler (MSVC 2022, GCC 9+, Clang 10+)
- ONNX Runtime 1.16+
- SQLite 3.40+
- Python 3.10+ (for training)
- PyTorch 2.1+ (for training)

### Current Phase: Research & Planning
- ✅ Architecture design complete
- ✅ Training procedures documented  
- ⏳ Dataset acquisition (next step)
- ⏳ Training infrastructure setup

See `research/NOMAD_MUSE_RESEARCH_PAPER.md` Section 11 for complete 40-week roadmap.

## 📊 Performance Targets

| Module | Latency | Accuracy/Quality | Memory | CPU |
|--------|---------|------------------|--------|-----|
| MIDI Gen | < 20 ms | > 80% acceptance | < 2 MB | < 10% |
| Classifier | < 10 ms | > 90% accuracy | < 1 MB | < 5% |
| Analyzer | 1-2 sec | > 4.0/5.0 helpfulness | < 0.5 MB | < 2% |
| **Total** | **< 20 ms** | **Varies** | **< 5 MB** | **< 15%** |

## 📝 Citation

```bibtex
@article{nomadmuse2025,
  title={Nomad Muse: A Distilled, Real-Time Symbolic Music Model for Adaptive Digital Audio Workstations},
  author={Dylan M.},
  journal={Nomad Studios Technical Reports},
  year={2025},
  note={Three-module AI system: MIDI generation, audio classification, spectral analysis}
}
```

## 📜 License
See the main NOMAD project LICENSE file.

---

**Author:** Dylan M. — Nomad Studios  
**Date:** 2025-10-27  
**Version:** 2.0 (Three-Module Integrated System)
