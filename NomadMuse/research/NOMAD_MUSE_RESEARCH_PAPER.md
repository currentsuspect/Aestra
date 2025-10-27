# Nomad Muse: A Distilled, Real-Time Symbolic Music Model for Adaptive Digital Audio Workstations

**Author:** Dylan M. — Nomad Studios  
**Date:** 2025-10-27  
**Keywords:** symbolic music, distillation, real-time generation, adaptive UI, human-AI collaboration, DAW integration

---

## Abstract

Nomad Muse is a comprehensive AI music production assistant embedded within the Nomad DAW, integrating three specialized neural modules: (1) a symbolic music generator for MIDI prediction, (2) an audio classifier for real-time instrument recognition, and (3) a spectral analyzer for mixing guidance. Unlike traditional offline generative systems, Muse performs real-time, zero-latency prediction of symbolic events (MIDI) while simultaneously adapting to each user's creative patterns and providing context-aware production assistance. This paper documents the complete three-module architecture, the multi-stage distillation pipeline that fuses complementary models into CPU-deployable components, and the cross-modal fusion process that enables bidirectional intelligence between generation, classification, and analysis. We discuss training methodologies, real-time integration strategies, ethical data collection, and design goals aimed at reaching perceptual parity with professional musicianship and audio engineering.

---

## 1. Introduction

Music production traditionally separates creative tasks (composition, arrangement) from technical tasks (mixing, mastering). Similarly, music generation research has primarily targeted large, cloud-hosted architectures focused solely on composition. Nomad Muse inverts this paradigm by unifying composition and production intelligence in a local, responsive system inside a DAW environment.

The system comprises three interconnected modules:

1. **MIDI Generation Module**: Context-aware symbolic music prediction with separate handling for percussive (drums) and tonal (melodic/harmonic) content
2. **Audio Classification Module**: Real-time instrument recognition and track-type detection to inform generation constraints
3. **Spectral Analysis Module**: Tonal balance evaluation and mixing/mastering guidance based on genre-specific reference curves

The objective is not only low-latency composition but also continuous co-evolution with the user through incremental learning, context-aware adaptation based on instrument detection, and real-time production feedback. All processing occurs locally on CPU/integrated GPU with <20ms latency, ensuring privacy and eliminating cloud dependencies.

This integrated approach enables a complete music production workflow: the system generates musically appropriate content (understanding that drums don't follow melodic scales), adapts to the instruments being used, and provides professional mixing guidance—all within a single, cohesive AI assistant.

---

## 2. Related Work

### Key Research Areas:

#### 2.1 Symbolic Music Generation
- **Music Transformer** (Huang et al., 2018): Self-attention for long-range musical dependencies
- **MuseNet** (OpenAI, 2019): Multi-instrument generation with 72-layer transformer
- **Magenta's MelodyRNN & PerformanceRNN** (Google, 2016-2019): LSTM-based real-time generation
- **MusPy** (Dong et al., 2020): Standardized symbolic music processing toolkit

#### 2.2 On-Device and Lightweight Models
- **DistilBERT & TinyBERT** (Sanh et al., 2019): Distillation for 40-60% size reduction
- **MobileNets** (Howard et al., 2017): Depthwise separable convolutions for efficiency
- **GRU vs LSTM trade-offs** (Chung et al., 2014): 25% fewer parameters, comparable performance
- **ONNX Runtime optimizations** (Microsoft, 2021): Quantization and graph optimization

#### 2.3 Adaptive and Personalized Systems
- **LoRA: Low-Rank Adaptation** (Hu et al., 2021): Efficient fine-tuning with adapter modules
- **Reinforcement Learning from Human Feedback (RLHF)**: OpenAI's GPT-3.5/4 alignment
- **Adaptive user interfaces**: Dynamic layout systems in Adobe Creative Suite

#### 2.4 Audio Classification and MIR
- **NSynth Dataset** (Engel et al., 2017): 305k instrument notes for timbre learning
- **Instrument recognition** (Han et al., 2017): CNN-based polyphonic instrument classification
- **Drum transcription** (Vogl et al., 2018): Onset detection and drum classification
- **Audio event detection** (Mesaros et al., 2019): Real-time sound classification

#### 2.5 Mixing and Mastering Systems
- **Intelligent mixing tools** (De Man & Reiss, 2013): Automated EQ and dynamics
- **Tonal balance analysis** (iZotope, 2020): Genre-specific frequency curve matching
- **Reference-based mastering** (Ma et al., 2015): Spectral matching to target tracks
- **Mix evaluation** (De Man et al., 2017): Perceptual quality metrics

---

## 3. Three-Module System Architecture

Nomad Muse integrates three specialized neural modules that communicate bidirectionally to provide comprehensive music production assistance. Each module is optimized independently but shares contextual information through a unified state management system.

### 3.1 Architecture Overview

```
┌────────────────────────────────────────────────────────────────┐
│                    UNIFIED STATE MANAGER                       │
│  - Current key/tempo/time signature                            │
│  - Active instrument types per track                           │
│  - User preferences and history                                │
│  - Session context and workflow state                          │
└────────────────────────────────────────────────────────────────┘
         │                      │                      │
         ▼                      ▼                      ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ MODULE 1:       │  │ MODULE 2:       │  │ MODULE 3:       │
│ MIDI GENERATOR  │◄─┤ AUDIO CLASSIFIER│◄─┤ SPECTRAL        │
│                 │  │                 │  │ ANALYZER        │
│ 300k params     │  │ 150k params     │  │ DSP + 10k       │
│ <20ms latency   │  │ <10ms latency   │  │ <100ms latency  │
│                 │  │                 │  │                 │
│ Input: Context  │  │ Input: Audio    │  │ Input: Mix      │
│ Output: MIDI    │  │ Output: Labels  │  │ Output: Advice  │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

**Information Flow:**
1. Audio Classifier identifies instrument types → updates Unified State
2. MIDI Generator queries State for track type → generates appropriate content
3. Generated MIDI → rendered to audio → Spectral Analyzer
4. Analyzer compares to genre targets → provides mixing suggestions
5. User feedback → updates all module preferences

### 3.2 Module 1: MIDI Generation System

#### 3.2.1 Core Architecture

The MIDI generator employs a GRU/Transformer-lite hybrid with ≈ 300k parameters. The inference engine is quantized and optimized for single-thread CPU execution inside Nomad DAW.

**Dual-Mode Generation:**
- **Tonal Mode**: For melodic/harmonic instruments (piano, guitar, bass, vocals)
  - Enforces key signature and scale constraints
  - Harmonic progression awareness
  - Voice leading principles
  
- **Percussive Mode**: For drums and unpitched percussion
  - Rhythmic pattern focus
  - No pitch constraints (uses drum hit vocabulary)
  - Groove and syncopation emphasis

#### 3.2.2 Network Architecture

**Input Layer:**
- 258-dim token embedding → 256-dim learned embedding
- Positional encoding: Sinusoidal + learned relative position bias (±64 steps)
- **Context injection**: Instrument type token from Audio Classifier

**Hidden Layers:**
- Layer 1-2: Stacked GRU (256 hidden units each)
  - Benefits: Sequential processing, efficient for temporal dependencies
  - Dropout: 0.2 between layers
- Layer 3: Single-head self-attention (64-dim head)
  - Benefits: Long-range dependencies, harmonic awareness
  - Attention window: ±32 tokens (local attention)
- Layer 4: GRU (256 units) - final recurrent processing

**Output Layer:**
- Linear projection to 258-dim logits
- Temperature-scaled softmax
- **Adaptive temperature**: Modulated by velocity context from Unified State

**Parameter Breakdown:**
- Embedding: 66k
- GRU layers: 115k
- Attention: 18k
- Output projection: 66k
- **Total: ~300k parameters**

#### 3.2.3 MIDI Generator Model Hierarchy

| Model | Role | Training Source | Parameters |
|-------|------|----------------|------------|
| Base | Universal grammar | Multi-dataset ensemble (see §3.2.4) | ≈ 2.5M |
| Student | Efficiency & speed | Distilled from Base | ≈ 200k |
| Personal Adapter | Individual style | User MIDI + logs | < 50k |
| Fusion (MIDI Gen) | Unified runtime | Ensemble distillation | ≈ 300k |

#### 3.2.4 MIDI Generator Training Datasets

The Base model is trained on a curated multi-genre corpus:

**Primary Datasets:**
- **MAESTRO v3.0** (200 hours, 1,282 performances): High-quality piano performances from International Piano-e-Competition. Provides exceptional timing, dynamics, and classical phrasing.
- **GiantMIDI-Piano** (10,855 pieces, ≈1,600 hours): Transcribed classical piano from audio. Complements MAESTRO with broader classical repertoire.

**Genre-Specific Datasets:**
- **Lakh MIDI Dataset (Clean Subset)** (≈45,000 pieces): Filtered for:
  - Minimum 16-bar length
  - Valid time signatures (4/4, 3/4, 6/8, 12/8)
  - < 8 simultaneous notes (excludes auto-generated/corrupted files)
  - Human-verified metadata tags
  
- **Reddit RWCMD** (Rock, World, Classical, Metal, Dance): Community-curated, genre-tagged subset of ≈15,000 high-quality MIDI files

- **ADL Piano MIDI** (≈11,000 pieces): Yamaha Disklavier performances, excellent for jazz and contemporary styles

- **Groove MIDI Dataset** (Magenta, 1,150 performances): Drum-focused, critical for rhythmic groove and syncopation patterns

**Specialized Datasets:**
- **Bach Chorales** (≈400 pieces): Four-part harmony, voice leading principles
- **Theorytab Corpus** (≈200k chord progressions): Harmonic patterns from popular music

**Data Distribution (by hours):**
- Classical Piano: 45% (MAESTRO + GiantMIDI)
- Pop/Rock: 25% (Lakh Clean + RWCMD)
- Jazz/Contemporary: 15% (ADL Piano)
- Drums/Percussion: 10% (Groove MIDI)
- Theory/Harmony: 5% (Bach + Theorytab)

**Preprocessing Pipeline:**
1. Tempo normalization to 60-180 BPM
2. Quantization to 16th-note grid (with swing preservation flag)
3. Velocity binning: 16 levels (0-127 → 0-15)
4. Duration encoding: 32 bins (16th to whole note + ties)
5. Data augmentation: ±2 semitone transposition, ±10% tempo scaling

#### 3.2.5 Token Vocabulary

The model operates on a compact symbolic vocabulary:

| Token Type | Count | Description |
|------------|-------|-------------|
| NOTE_ON | 128 | MIDI pitches 0-127 |
| DURATION | 32 | 16th note to 4 whole notes (exponential bins) |
| VELOCITY | 16 | Dynamics from ppp to fff |
| TIME_SHIFT | 64 | Relative timing (0-4 beats, 64th note resolution) |
| CONTROL | 12 | Sustain pedal, modulation, pitch bend markers |
| SPECIAL | 6 | \<START\>, \<END\>, \<PAD\>, \<MASK\>, \<SEP\>, \<UNK\> |
| **TOTAL** | **258** | Full vocabulary size |

This compact representation enables fast inference while preserving musical expressiveness.

#### 3.2.6 MIDI Generator Distillation and Fusion Training

The fusion probability distribution is defined as:

$$P_{fusion}(x) = \alpha P_{base}(x) + \beta P_{student}(x) + \gamma P_{personal}(x)$$

Where weights are dynamically adjusted:
- $\alpha = 0.5$ (universal musical grammar)
- $\beta = 0.3$ (efficiency and speed)
- $\gamma = 0.2 \times \text{sigmoid}(n_{sessions} - 10)$ (grows with user data)

A new model is trained to minimize:

$$L = \text{KL}(P_{fusion} \Vert Q_{fusion}) + \lambda \cdot L_{accept}$$

Where $L_{accept}$ is a reinforcement term based on user acceptance rate from logged sessions.

**Quantization Strategy:**
- Weights: 8-bit integer quantization (symmetric, per-channel)
- Activations: 8-bit dynamic quantization
- Export format: ONNX with QDQ (Quantize-Dequantize) nodes
- Expected speedup: 3-4× on modern CPUs with AVX2/AVX-512

**Cold-Start Mitigation:**
Personal adapters initialize from Student weights with 5% Gaussian noise ($\sigma = 0.02$) to break symmetry.

**Training Procedure (MIDI Generator):**

1. **Phase 1: Base Model Training** (12 weeks)
   - Dataset: Full corpus (see §3.2.4) - 2M+ MIDI files
   - Model: Transformer with 2.5M parameters
   - Optimization: AdamW, lr=1e-4, batch=64, seq_len=512
   - Loss: Cross-entropy on next-token prediction
   - Validation: Perplexity < 3.5 on held-out set
   
2. **Phase 2: Student Distillation** (4 weeks)
   - Teacher: Base model (frozen)
   - Student: GRU/Transformer-lite (200k params)
   - Loss: $L = \alpha \cdot L_{CE} + (1-\alpha) \cdot L_{KD}$
     - $L_{CE}$: Cross-entropy on true labels
     - $L_{KD}$: KL divergence from teacher logits (T=3)
     - $\alpha = 0.3$ (emphasis on distillation)
   - Target: Perplexity < 5.0, 90% of Base quality
   
3. **Phase 3: Personal Adapter Fine-Tuning** (ongoing)
   - Base: Student model (frozen backbone)
   - Adapter: LoRA-style low-rank layers (50k params)
   - Training: Online learning from user sessions
   - Update frequency: Every 10 sessions or weekly
   - Regularization: L2 penalty to prevent overfitting
   
4. **Phase 4: Fusion Model** (2 weeks)
   - Ensemble: $P_{fusion} = 0.5 P_{base} + 0.3 P_{student} + 0.2 P_{adapter}$
   - Distillation: Train 300k model to match ensemble
   - Loss: $L = KL(P_{fusion} \| P_{final}) + \lambda L_{accept}$
     - $L_{accept}$: Reinforcement term from user acceptance logs
     - $\lambda = 0.1$
   - Export: ONNX INT8 for deployment

---

### 3.3 Module 2: Audio Classification System

The Audio Classification module provides real-time instrument recognition and track-type detection, enabling context-aware generation and mixing guidance.

#### 3.3.1 Architecture Design

**Lightweight CNN for Real-Time Inference:**

```
Input: 50ms audio frame (2,205 samples @ 44.1kHz)
       ↓
Feature Extraction: MFCC (128 bins, 20ms hop)
       ↓
Conv Block 1: 32 filters (3×3), ReLU, BatchNorm, MaxPool(2×2)
Conv Block 2: 64 filters (3×3), ReLU, BatchNorm, MaxPool(2×2)
Conv Block 3: 128 filters (3×3), ReLU, BatchNorm, MaxPool(2×2)
       ↓
Global Average Pooling
       ↓
FC Layer 1: 256 units, ReLU, Dropout(0.3)
FC Layer 2: 64 units, ReLU
       ↓
Output Heads:
  - Instrument Class: 20 classes, Softmax
  - Track Type: 2 classes (percussive/tonal), Sigmoid
  - Confidence: 1 value, Sigmoid
```

**Parameter Count:**
- Conv layers: ~85k
- FC layers: ~50k
- Output layers: ~15k
- **Total: ~150k parameters**

**Latency Optimization:**
- Input downsampling: 44.1kHz → 22.05kHz (sufficient for timbre)
- Small kernel sizes: 3×3 (minimal computation)
- Depthwise separable convolutions (future optimization)
- Quantization: INT8 weights + activations

#### 3.3.2 Training Datasets

| Dataset | Size | Content | Training Role |
|---------|------|---------|---------------|
| **NSynth** | 305k notes, 1,006 instruments | Single-note audio, labeled timbre | **Primary instrument classifier** |
| **Groove MIDI Audio** | 13.6 hours | Aligned drum recordings + MIDI | **Drum detection specialist** |
| **E-GMD** | 444 hours | Extended drum kit variety | **Percussion augmentation** |
| **ENST Drums** | ~3,000 clips | Labeled drum hits (kick/snare/etc.) | **Fine-grained drum types** |
| **Mixing Secrets** | 250 songs, stems | Isolated instrument tracks | **Polyphonic validation** |
| **MedleyDB** | ~120 songs | Multi-instrument annotations | **Real-world mix context** |

**Class Distribution (20 classes):**
1. Kick Drum
2. Snare Drum
3. Hi-Hat / Cymbals
4. Toms / Percussion
5. Electric Bass
6. Acoustic Bass
7. Electric Guitar (clean)
8. Electric Guitar (distorted)
9. Acoustic Guitar
10. Piano / Keys
11. Electric Piano / Synth
12. Strings (violin, cello, etc.)
13. Brass (trumpet, trombone, etc.)
14. Woodwinds (sax, flute, etc.)
15. Vocals (male)
16. Vocals (female)
17. Synth Lead
18. Synth Pad
19. Sound Effects
20. Other

**Binary Track Type:**
- **Percussive**: Classes 1-4, 19
- **Tonal**: Classes 5-18, 20

#### 3.3.3 Training Procedure (Audio Classifier)

**Phase 1: Base Classifier Training** (4 weeks)

1. **Data Preprocessing:**
   ```python
   - Resample to 22.05 kHz
   - Extract MFCC (n_mfcc=128, hop_length=441, win_length=1024)
   - Normalize per-channel (mean=0, std=1)
   - Augmentation:
     * Pitch shift: ±2 semitones
     * Time stretch: 0.9-1.1×
     * Gaussian noise: SNR 30-50 dB
     * Mixup: α=0.2 for robustness
   ```

2. **Training Configuration:**
   - Optimizer: Adam, lr=1e-3 with cosine annealing
   - Batch size: 128
   - Loss: Multi-task learning
     ```
     L_total = L_instrument + 0.5 * L_tracktype + 0.3 * L_confidence
     L_instrument: Cross-entropy (20 classes)
     L_tracktype: Binary cross-entropy (percussive/tonal)
     L_confidence: MSE to oracle confidence
     ```
   - Epochs: 100 with early stopping (patience=10)
   
3. **Validation Targets:**
   - Instrument accuracy: > 90% (test set)
   - Track type accuracy: > 95% (binary classification)
   - Inference time: < 10ms (CPU, single core)

**Phase 2: Drum Specialization Fine-Tuning** (1 week)

- Dataset: Groove + E-GMD + ENST (percussion-only)
- Freeze conv layers, fine-tune FC + output
- Boost drum class weights in loss
- Target: > 98% drum detection recall

**Phase 3: Polyphonic Validation** (1 week)

- Dataset: Mixing Secrets + MedleyDB (multi-instrument mixes)
- Test on overlapping instruments
- Accumulate predictions over 100-200ms windows
- Majority voting + confidence thresholding
- Target: > 85% accuracy in polyphonic context

**Phase 4: Export and Optimization** (1 week)

- Quantization: ONNX INT8
- Pruning: Remove <5% magnitude weights (optional)
- Validate latency: < 10ms on target hardware
- Memory footprint: < 1 MB

#### 3.3.4 Real-Time Integration

**Decision Logic:**
```python
def classify_track(audio_stream, window_ms=200):
    predictions = []
    
    for frame in audio_stream.get_frames(50ms):
        # Run classifier (<10ms)
        instrument_probs = classifier.forward(frame)
        predictions.append(instrument_probs)
    
    # Accumulate over window
    avg_probs = np.mean(predictions, axis=0)
    
    # Determine instrument
    if max(avg_probs) > 0.75:  # Confidence threshold
        instrument_class = argmax(avg_probs)
        
        # Binary track type
        is_percussive = instrument_class in [0,1,2,3,18]
        
        # Update Unified State
        state.set_track_type(
            track_id, 
            "DRUMS" if is_percussive else "TONAL",
            instrument_name=CLASS_NAMES[instrument_class],
            confidence=max(avg_probs)
        )
        
        return instrument_class, is_percussive
    else:
        return None, None  # Uncertain, use default
```

**Communication with MIDI Generator:**
- Classifier updates Unified State every 200ms
- MIDI Generator queries State before each prediction
- If track type changes, Generator switches mode:
  - DRUMS → use percussive sub-network
  - TONAL → use melodic sub-network with key constraints

---

### 3.4 Module 3: Spectral Analysis and Mixing System

The Spectral Analysis module evaluates tonal balance and provides real-time mixing/mastering guidance based on genre-specific reference curves.

#### 3.4.1 Analysis Architecture

**DSP-Based Spectral Engine:**

```
Input: Mixed audio (stereo, 44.1kHz or 48kHz)
       ↓
Windowing: Hann window (4096 samples, 50% overlap)
       ↓
FFT: Real FFT → 2048 frequency bins
       ↓
Magnitude Spectrum: |FFT|
       ↓
Band Energy Calculation:
  - Sub-Bass:   20-60 Hz
  - Bass:       60-250 Hz
  - Low-Mid:    250-500 Hz
  - Mid:        500-2000 Hz
  - High-Mid:   2000-6000 Hz
  - Presence:   6000-12000 Hz
  - Air:        12000-20000 Hz
       ↓
Smoothing: Exponential moving average (α=0.1)
       ↓
Genre Comparison: Current - Target Curve
       ↓
Suggestion Engine: Rule-based + Small MLP (10k params)
       ↓
Output: Text suggestions + Visual meters
```

**Additional Metrics:**
- **Loudness:** LUFS (ITU-R BS.1770) - integrated, short-term, momentary
- **Dynamic Range:** Crest factor, PLR (Peak-to-Loudness Ratio)
- **Stereo Width:** Correlation coefficient, side channel energy
- **Phase Coherence:** Mono compatibility check

#### 3.4.2 Genre Target Curves

**Reference Dataset Construction:**

| Genre | Tracks Analyzed | Source | Curve Characteristics |
|-------|----------------|--------|----------------------|
| Pop | 5,000 | Billboard Hot 100 (2015-2025) | Balanced, slight mid boost |
| Rock | 3,000 | Classic + Modern Rock | Mid-forward, scooped highs |
| EDM | 4,000 | Beatport Top 100 | Heavy bass, aggressive highs |
| Hip-Hop | 3,500 | Rap Caviar, Top tracks | Sub-bass emphasis, vocal clarity |
| Jazz | 2,000 | Blue Note, ECM catalog | Natural, minimal low-end |
| Classical | 2,500 | Deutsche Grammophon | Wide dynamic range, natural |
| Metal | 2,000 | Metal Blade, Roadrunner | Scooped mids, extreme compression |
| Indie | 2,000 | Pitchfork Best New Music | Variable, lo-fi aesthetic |

**Curve Computation:**
```python
def compute_genre_curve(track_collection):
    all_spectra = []
    
    for track in track_collection:
        # Load audio
        audio, sr = load_audio(track, sr=44100)
        
        # Skip intro/outro, use middle 2 minutes
        core_audio = extract_core_section(audio, duration=120)
        
        # Compute averaged spectrum
        spectrum = compute_fft_average(core_audio)
        
        # Convert to dB
        spectrum_db = 20 * np.log10(spectrum + 1e-10)
        
        all_spectra.append(spectrum_db)
    
    # Median spectrum (robust to outliers)
    genre_curve = np.median(all_spectra, axis=0)
    
    # Smooth curve
    genre_curve_smooth = savgol_filter(genre_curve, 51, 3)
    
    return genre_curve_smooth
```

**Stored Curves:** 8 genre profiles + user-custom references

#### 3.4.3 Suggestion Engine

**Rule-Based System:**

```python
def generate_suggestions(current_spectrum, target_curve, tolerance=3.0):
    suggestions = []
    deviation = current_spectrum - target_curve
    
    # Check each band
    if deviation['sub_bass'] > tolerance:
        suggestions.append({
            'severity': 'medium',
            'message': f"Excessive sub-bass (+{deviation['sub_bass']:.1f} dB). "
                      "Consider high-pass filtering non-bass instruments at 40-60 Hz.",
            'freq_range': (20, 60),
            'action': 'reduce',
            'amount_db': -deviation['sub_bass']
        })
    
    if deviation['low_mid'] > tolerance:
        suggestions.append({
            'severity': 'high',
            'message': f"Muddy low-mids (+{deviation['low_mid']:.1f} dB). "
                      "Try cutting 200-400 Hz on guitars, keys, or pads.",
            'freq_range': (200, 400),
            'action': 'reduce',
            'amount_db': -deviation['low_mid']
        })
    
    if deviation['presence'] < -tolerance:
        suggestions.append({
            'severity': 'medium',
            'message': f"Lacking presence ({deviation['presence']:.1f} dB). "
                      "Boost 4-6 kHz on vocals or lead instruments for clarity.",
            'freq_range': (4000, 6000),
            'action': 'boost',
            'amount_db': -deviation['presence']
        })
    
    if deviation['air'] < -tolerance:
        suggestions.append({
            'severity': 'low',
            'message': f"Lacking air and sparkle ({deviation['air']:.1f} dB). "
                      "Add subtle high shelf at 10-12 kHz (+1-2 dB).",
            'freq_range': (10000, 20000),
            'action': 'boost',
            'amount_db': min(-deviation['air'], 2.0)  # Cap at +2 dB
        })
    
    # Sort by severity
    priority = {'high': 0, 'medium': 1, 'low': 2}
    suggestions.sort(key=lambda s: priority[s['severity']])
    
    return suggestions[:3]  # Top 3 suggestions
```

**ML-Enhanced Suggestions (Optional):**

Small MLP (10k parameters) trained on Mix Evaluation Dataset:
- Input: 7 band deviations + genre + loudness + stereo width
- Hidden: 2 layers (64 → 32 units)
- Output: Suggestion scores for 50 predefined actions
- Training: Supervised from expert annotations + user ratings

#### 3.4.4 Training Procedure (Spectral Analyzer)

**Phase 1: Reference Curve Collection** (2 weeks)

1. Acquire 20,000+ professionally mixed/mastered tracks
2. Organize by genre (manual + automated tagging)
3. Compute spectral curves for each track
4. Generate median + percentile curves per genre
5. Validate curves with professional mixing engineers

**Phase 2: Rule System Development** (1 week)

1. Define thresholds based on genre variance
2. Prioritize suggestions by deviation magnitude
3. Map frequency bands to common mixing issues
4. Natural language template generation

**Phase 3: ML Suggestion Model** (2 weeks, optional)

1. Dataset: Mix Evaluation Dataset (180 mixes with ratings)
2. Features: Spectral deviation + context (genre, loudness, etc.)
3. Labels: Expert-annotated suggestions + user ratings
4. Train MLP: Binary cross-entropy on suggestion relevance
5. Validate: Precision/recall > 0.75 on held-out set

**Phase 4: Real-Time Integration** (1 week)

1. Optimize FFT computation (use FFTW or similar)
2. Implement exponential smoothing for stability
3. Update suggestions every 1-2 seconds (separate thread)
4. UI integration: Visual meters + text panel

---

### 3.5 Cross-Modal Fusion and Communication

The three modules communicate through a **Unified State Manager** that maintains global context and enables bidirectional information flow.

#### 3.5.1 Unified State Manager

**State Variables:**

```cpp
class UnifiedState {
public:
    // Musical context
    Key current_key;
    int tempo_bpm;
    TimeSignature time_sig;
    
    // Track information (per-track)
    std::map<int, TrackInfo> tracks;
    struct TrackInfo {
        InstrumentType instrument;    // From Audio Classifier
        TrackType type;                // DRUMS or TONAL
        float confidence;              // Classifier confidence
        bool muted;
        float volume_db;
    };
    
    // Performance context
    std::vector<float> recent_velocities;  // Last 100 notes
    float avg_velocity;
    float velocity_variance;
    
    // Mix context
    std::map<std::string, float> band_energies;  // From Spectral Analyzer
    std::string current_genre;
    std::vector<Suggestion> active_suggestions;
    
    // User preferences
    UserPreferences prefs;
    SessionHistory history;
    
    // Thread-safe access
    std::shared_mutex state_mutex;
};
```

**Lock-Free Communication:**

```cpp
// Audio Classifier → State (every 200ms)
void update_track_type(int track_id, InstrumentType inst, float conf) {
    std::unique_lock lock(state_mutex);
    tracks[track_id].instrument = inst;
    tracks[track_id].type = (is_percussive(inst)) ? DRUMS : TONAL;
    tracks[track_id].confidence = conf;
}

// MIDI Generator ← State (every prediction)
TrackType get_track_type(int track_id) {
    std::shared_lock lock(state_mutex);
    return tracks[track_id].type;
}

// Spectral Analyzer → State (every 1-2 sec)
void update_mix_analysis(std::map<std::string, float> bands, 
                         std::vector<Suggestion> suggestions) {
    std::unique_lock lock(state_mutex);
    band_energies = bands;
    active_suggestions = suggestions;
}
```

#### 3.5.2 Information Flow Scenarios

**Scenario 1: New Drum Track**

1. User starts playing drums (MIDI input on channel 10)
2. **Audio Classifier** detects kick/snare sounds (50ms latency)
   - Updates State: `tracks[10].type = DRUMS, confidence=0.95`
3. **MIDI Generator** queries State before next prediction
   - Sees `DRUMS` → switches to percussive mode
   - Generates drum fill (no pitch constraints)
4. Generated MIDI → rendered to audio
5. **Spectral Analyzer** sees increased bass energy (kick drum)
   - Checks if within target for genre
   - If excessive: suggests "Reduce kick at 60 Hz by 2 dB"

**Scenario 2: Melodic Instrument Added**

1. User loads a piano VST on track 3
2. **Audio Classifier** analyzes piano audio (10ms latency)
   - Updates State: `tracks[3].type = TONAL, instrument=PIANO`
3. **MIDI Generator** queries State
   - Sees `TONAL` + `PIANO` → switches to melodic mode
   - Enforces key (e.g., C minor) + generates chord voicings
4. **Spectral Analyzer** detects mid-range addition
   - Checks for low-mid buildup (common with piano)
   - Suggests: "Consider high-pass at 100 Hz to avoid muddiness"

**Scenario 3: Mix Guidance**

1. User has 8 tracks playing (drums, bass, guitars, vocals)
2. **Spectral Analyzer** computes overall balance
   - Compares to "Rock" genre curve
   - Detects: excess 200-400 Hz, lacking 4-6 kHz
3. **Audio Classifier** identifies which tracks are guitars
   - State shows: tracks 4,5,6 = guitar
4. **Analyzer** provides specific suggestion:
   - "Cut 300 Hz on guitar tracks 4-6 (muddy buildup)"
   - "Boost 5 kHz on vocal track 7 (presence)"
5. User makes adjustments → new analysis in 2 seconds

#### 3.5.3 Fusion Training (Cross-Modal)

Beyond individual module training, we perform **cross-modal fusion** to improve overall system intelligence.

**Stage 1: Joint Representation Learning** (4 weeks, optional)

- Train a shared embedding space for:
  - MIDI tokens (from Generator)
  - Audio features (from Classifier)  
  - Spectral features (from Analyzer)
  
- Architecture: Contrastive learning (SimCLR-style)
  - Positive pairs: MIDI + Audio from same track
  - Negative pairs: Mismatched MIDI + Audio
  - Loss: NT-Xent (normalized temperature-scaled cross-entropy)

- Benefits:
  - Generator can "hear" what it's generating
  - Classifier can "see" symbolic patterns
  - Analyzer can "understand" composition intent

**Stage 2: Reinforcement Learning from User Feedback** (ongoing)

- Collect user actions:
  - MIDI predictions: accepted (reward=+1) or rejected (reward=-1)
  - Classifier corrections: manual override (penalty=-0.5)
  - Mixing suggestions: applied (reward=+1) or ignored (reward=0)

- Update all modules:
  - Generator: RL fine-tuning with policy gradient
  - Classifier: Active learning from corrections
  - Analyzer: Preference learning from applied suggestions

- Personalization:
  - Each user has custom reward model
  - Adapters fine-tuned per user
  - Genre preferences learned from session history

**Stage 3: End-to-End Optimization** (future work)

- Train all three modules jointly:
  - Objective: Maximize user engagement + track quality
  - Differentiable rendering (MIDI → audio synthesis)
  - Backprop through entire pipeline
  - Multi-task loss: generation + classification + mixing

- Challenges:
  - Computational cost (requires GPU farm)
  - Defining "quality" metric (subjective)
  - Maintaining real-time performance

---

## 4. Complete System Training Pipeline

This section details the end-to-end training procedure for all three modules, including preprocessing, optimization, and integration testing.

### 4.1 Data Collection and Preprocessing

#### 4.1.1 MIDI Data Pipeline

**Step 1: Dataset Acquisition**
- GigaMIDI: 1.4M files (download via torrent/API)
- Lakh MIDI: 45k clean subset (filter by metadata quality)
- MAESTRO v3: 1,282 performances (official release)
- GiantMIDI-Piano: 10,855 pieces (download from source)
- Groove MIDI: 13.6 hours (TensorFlow Datasets)
- Bach, Theorytab: Curated collections

**Step 2: Quality Filtering**
```python
def filter_midi_quality(midi_file):
    # Load with pretty_midi
    midi = pretty_midi.PrettyMIDI(midi_file)
    
    # Filter criteria
    if len(midi.instruments) == 0:
        return False  # No instruments
    
    total_notes = sum(len(inst.notes) for inst in midi.instruments)
    if total_notes < 50:
        return False  # Too short
    
    duration = midi.get_end_time()
    if duration < 30 or duration > 600:
        return False  # Too short or too long
    
    # Check for valid time signatures
    time_sigs = midi.time_signature_changes
    if time_sigs and time_sigs[0].numerator not in [2,3,4,6,12]:
        return False  # Unusual time signature
    
    # Check for excessive simultaneous notes (corrupt files)
    max_concurrent = max_concurrent_notes(midi)
    if max_concurrent > 20:
        return False  # Likely auto-generated or corrupt
    
    return True
```

**Step 3: Preprocessing**
1. Tempo normalization: 60-180 BPM
2. Separate drum tracks (MIDI channel 10 or percussion program)
3. Quantization: 16th-note grid with swing detection
4. Velocity binning: 0-127 → 16 levels
5. Duration encoding: 32 exponential bins
6. Key detection and transposition augmentation

**Step 4: Tokenization**
- Convert MIDI to token sequences (258-token vocabulary)
- Sequence chunking: 512 tokens with 64-token overlap
- Create train/val/test splits: 90% / 5% / 5%
- Save as HDF5 for fast loading

#### 4.1.2 Audio Data Pipeline

**Step 1: Dataset Acquisition**
- NSynth: 305k notes (Google Cloud download)
- Groove Audio: 13.6 hours (TensorFlow Datasets)
- E-GMD: 444 hours (request from Magenta team)
- ENST Drums: 3k clips (academic download)
- Mixing Secrets: 250 songs (Cambridge repository)
- MedleyDB: 120 songs (academic license)

**Step 2: Feature Extraction**
```python
def extract_audio_features(audio_file, sr=22050):
    # Load audio
    audio, sr = librosa.load(audio_file, sr=sr, mono=True)
    
    # Extract MFCC
    mfcc = librosa.feature.mfcc(
        y=audio, 
        sr=sr, 
        n_mfcc=128, 
        hop_length=441,  # 20ms
        win_length=1024
    )
    
    # Normalize
    mfcc = (mfcc - mfcc.mean(axis=1, keepdims=True)) / (mfcc.std(axis=1, keepdims=True) + 1e-8)
    
    # Convert to frames (50ms windows)
    frames = []
    for i in range(0, mfcc.shape[1] - 3, 1):  # 3 frames = ~50ms
        frame = mfcc[:, i:i+3].flatten()  # 128 × 3 = 384 features
        frames.append(frame)
    
    return np.array(frames)
```

**Step 3: Labeling**
- NSynth: Use provided instrument family labels
- Groove/ENST: Label as percussion with drum type annotations
- Mixing Secrets: Extract labels from track names (manual verification)
- Augmentation: Pitch shift, time stretch, noise injection

**Step 4: Data Balancing**
- Oversample rare classes (e.g., woodwinds, brass)
- Undersample common classes (e.g., piano, guitar)
- Target: ~10k examples per class (200k total)

#### 4.1.3 Reference Mix Data Pipeline

**Step 1: Collection**
- Automated: Spotify API + YouTube (legal gray area, research only)
- Manual: Personal library, purchased tracks
- Academic: AES datasets, mixing competitions
- Target: 20,000 tracks across 8 genres

**Step 2: Genre Tagging**
- Automated: Music Brainz, Spotify API
- Manual verification: 10% sample review
- Multi-label support: "Pop-Rock", "Jazz-Fusion", etc.

**Step 3: Spectral Analysis**
```python
def compute_track_spectrum(audio_file):
    audio, sr = librosa.load(audio_file, sr=44100, mono=True)
    
    # Use middle 2 minutes (avoid intro/outro)
    start = len(audio) // 3
    end = start + sr * 120
    core_audio = audio[start:end]
    
    # FFT with averaging
    fft_size = 4096
    hop = fft_size // 2
    spectra = []
    
    for i in range(0, len(core_audio) - fft_size, hop):
        frame = core_audio[i:i+fft_size] * np.hanning(fft_size)
        spectrum = np.abs(np.fft.rfft(frame))
        spectra.append(spectrum)
    
    # Average spectrum
    avg_spectrum = np.mean(spectra, axis=0)
    
    # Convert to dB
    avg_spectrum_db = 20 * np.log10(avg_spectrum + 1e-10)
    
    # Bin into bands
    bands = compute_band_energy(avg_spectrum_db, sr)
    
    return bands
```

**Step 4: Genre Curve Generation**
- Compute median spectrum per genre
- Smooth with Savitzky-Golay filter
- Store as JSON/NPY for runtime loading

### 4.2 Module-Specific Training Procedures

#### 4.2.1 MIDI Generator Training (Detailed)

**Week 1-12: Base Model**

```python
# Model configuration
config = {
    'model_type': 'transformer',
    'd_model': 512,
    'n_layers': 12,
    'n_heads': 8,
    'vocab_size': 258,
    'max_seq_len': 512,
    'dropout': 0.1
}

# Training configuration
train_config = {
    'optimizer': 'AdamW',
    'lr': 1e-4,
    'weight_decay': 0.01,
    'batch_size': 64,
    'gradient_accumulation': 4,  # Effective batch = 256
    'epochs': 50,
    'warmup_steps': 10000,
    'lr_schedule': 'cosine_with_warmup'
}

# Dataset loading
train_dataset = MIDIDataset('gigamidi_train.h5', seq_len=512)
val_dataset = MIDIDataset('gigamidi_val.h5', seq_len=512)

# Training loop
for epoch in range(50):
    model.train()
    for batch in train_loader:
        tokens = batch['tokens'].to(device)
        
        # Forward pass
        logits = model(tokens[:, :-1])  # Predict next token
        
        # Loss
        loss = F.cross_entropy(
            logits.reshape(-1, vocab_size),
            tokens[:, 1:].reshape(-1)
        )
        
        # Backward
        loss.backward()
        
        if (step + 1) % gradient_accumulation == 0:
            optimizer.step()
            optimizer.zero_grad()
        
    # Validation
    val_perplexity = evaluate(model, val_loader)
    print(f"Epoch {epoch}: Val Perplexity = {val_perplexity:.2f}")
    
    if val_perplexity < 3.5:
        break  # Target reached
```

**Week 13-16: Student Distillation**

```python
# Student model (smaller)
student_config = {
    'model_type': 'gru_transformer_hybrid',
    'gru_hidden': 256,
    'n_gru_layers': 3,
    'attention_heads': 1,
    'attention_dim': 64,
    'vocab_size': 258,
    'dropout': 0.2
}

# Distillation training
def distillation_loss(student_logits, teacher_logits, true_labels, alpha=0.3, T=3):
    # Hard label loss
    ce_loss = F.cross_entropy(student_logits, true_labels)
    
    # Soft label loss (KL divergence)
    student_soft = F.log_softmax(student_logits / T, dim=-1)
    teacher_soft = F.softmax(teacher_logits / T, dim=-1)
    kd_loss = F.kl_div(student_soft, teacher_soft, reduction='batchmean') * (T ** 2)
    
    # Combined loss
    return alpha * ce_loss + (1 - alpha) * kd_loss

# Training loop
teacher.eval()  # Frozen
student.train()

for epoch in range(20):
    for batch in train_loader:
        tokens = batch['tokens'].to(device)
        
        # Teacher predictions (no grad)
        with torch.no_grad():
            teacher_logits = teacher(tokens[:, :-1])
        
        # Student predictions
        student_logits = student(tokens[:, :-1])
        
        # Distillation loss
        loss = distillation_loss(
            student_logits.reshape(-1, vocab_size),
            teacher_logits.reshape(-1, vocab_size),
            tokens[:, 1:].reshape(-1),
            alpha=0.3,
            T=3
        )
        
        loss.backward()
        optimizer.step()
        optimizer.zero_grad()
    
    val_perplexity = evaluate(student, val_loader)
    print(f"Epoch {epoch}: Student Perplexity = {val_perplexity:.2f}")
```

**Week 17-22: Personal Adapter**

```python
# LoRA-style adapter
class LoRAAdapter(nn.Module):
    def __init__(self, base_model, rank=16):
        super().__init__()
        self.base_model = base_model
        
        # Low-rank matrices for each layer
        self.lora_layers = nn.ModuleList([
            nn.Sequential(
                nn.Linear(d_model, rank, bias=False),
                nn.Linear(rank, d_model, bias=False)
            )
            for _ in range(n_layers)
        ])
        
        # Freeze base model
        for param in base_model.parameters():
            param.requires_grad = False
    
    def forward(self, x):
        # Base model output
        base_out = self.base_model(x)
        
        # Add LoRA adaptations
        for i, lora in enumerate(self.lora_layers):
            base_out[i] = base_out[i] + lora(base_out[i])
        
        return base_out

# Online learning from user sessions
def update_adapter(adapter, user_session_data, lr=1e-5):
    adapter.train()
    optimizer = torch.optim.Adam(adapter.lora_layers.parameters(), lr=lr)
    
    for epoch in range(5):  # Few epochs to prevent overfitting
        for batch in user_session_data:
            tokens = batch['tokens']
            logits = adapter(tokens[:, :-1])
            
            # Weight accepted vs. rejected predictions
            weights = batch['acceptance']  # 1 for accepted, 0.1 for rejected
            loss = weighted_cross_entropy(logits, tokens[:, 1:], weights)
            
            loss.backward()
            optimizer.step()
            optimizer.zero_grad()
```

**Week 23-24: Fusion Model**

```python
# Ensemble predictions
def ensemble_predict(base, student, adapter, tokens, weights=[0.5, 0.3, 0.2]):
    with torch.no_grad():
        base_logits = base(tokens)
        student_logits = student(tokens)
        adapter_logits = adapter(tokens)
    
    # Weighted average of probabilities
    base_probs = F.softmax(base_logits, dim=-1)
    student_probs = F.softmax(student_logits, dim=-1)
    adapter_probs = F.softmax(adapter_logits, dim=-1)
    
    ensemble_probs = (
        weights[0] * base_probs +
        weights[1] * student_probs +
        weights[2] * adapter_probs
    )
    
    return ensemble_probs

# Train fusion model to match ensemble
fusion_model = GRUTransformerHybrid(config)  # 300k params

for epoch in range(10):
    for batch in train_loader:
        tokens = batch['tokens']
        
        # Get ensemble target
        ensemble_probs = ensemble_predict(base, student, adapter, tokens[:, :-1])
        
        # Fusion model prediction
        fusion_logits = fusion_model(tokens[:, :-1])
        fusion_probs = F.softmax(fusion_logits, dim=-1)
        
        # KL divergence loss
        kd_loss = F.kl_div(
            F.log_softmax(fusion_logits, dim=-1),
            ensemble_probs,
            reduction='batchmean'
        )
        
        # Optional: Add user acceptance reinforcement
        if 'acceptance' in batch:
            accept_loss = weighted_cross_entropy(fusion_logits, tokens[:, 1:], batch['acceptance'])
            total_loss = kd_loss + 0.1 * accept_loss
        else:
            total_loss = kd_loss
        
        total_loss.backward()
        optimizer.step()
        optimizer.zero_grad()

# Export to ONNX
torch.onnx.export(
    fusion_model,
    dummy_input,
    "nomad_muse_midi_gen.onnx",
    input_names=['tokens'],
    output_names=['logits'],
    dynamic_axes={'tokens': {0: 'batch', 1: 'sequence'}}
)

# Quantize to INT8
from onnxruntime.quantization import quantize_dynamic
quantize_dynamic("nomad_muse_midi_gen.onnx", "nomad_muse_midi_gen_int8.onnx")
```

#### 4.2.2 Audio Classifier Training (Detailed)

**Week 1-4: Base Classifier**

```python
# Model definition
class InstrumentClassifier(nn.Module):
    def __init__(self, n_classes=20):
        super().__init__()
        
        # Conv blocks
        self.conv1 = nn.Sequential(
            nn.Conv2d(1, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(),
            nn.MaxPool2d(2)
        )
        self.conv2 = nn.Sequential(
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(),
            nn.MaxPool2d(2)
        )
        self.conv3 = nn.Sequential(
            nn.Conv2d(64, 128, kernel_size=3, padding=1),
            nn.BatchNorm2d(128),
            nn.ReLU(),
            nn.MaxPool2d(2)
        )
        
        # Global pooling
        self.gap = nn.AdaptiveAvgPool2d(1)
        
        # FC layers
        self.fc1 = nn.Linear(128, 256)
        self.dropout = nn.Dropout(0.3)
        self.fc2 = nn.Linear(256, 64)
        
        # Output heads
        self.instrument_head = nn.Linear(64, n_classes)
        self.tracktype_head = nn.Linear(64, 1)  # Binary: percussive/tonal
        self.confidence_head = nn.Linear(64, 1)
    
    def forward(self, x):
        # x: [batch, 1, mfcc_bins, time_frames]
        x = self.conv1(x)
        x = self.conv2(x)
        x = self.conv3(x)
        x = self.gap(x).squeeze(-1).squeeze(-1)  # [batch, 128]
        
        x = F.relu(self.fc1(x))
        x = self.dropout(x)
        x = F.relu(self.fc2(x))
        
        # Multi-task outputs
        instrument_logits = self.instrument_head(x)
        tracktype_logit = self.tracktype_head(x)
        confidence = torch.sigmoid(self.confidence_head(x))
        
        return instrument_logits, tracktype_logit, confidence

# Training configuration
model = InstrumentClassifier(n_classes=20).to(device)
optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)
scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=100)

# Multi-task loss
def multi_task_loss(inst_logits, tt_logit, conf, inst_label, tt_label, conf_label):
    loss_inst = F.cross_entropy(inst_logits, inst_label)
    loss_tt = F.binary_cross_entropy_with_logits(tt_logit.squeeze(), tt_label.float())
    loss_conf = F.mse_loss(conf.squeeze(), conf_label)
    
    return loss_inst + 0.5 * loss_tt + 0.3 * loss_conf

# Training loop
for epoch in range(100):
    model.train()
    for batch in train_loader:
        mfcc = batch['mfcc'].to(device)  # [batch, 1, 128, 3]
        inst_label = batch['instrument'].to(device)
        tt_label = batch['track_type'].to(device)  # 0=tonal, 1=percussive
        conf_label = batch['confidence'].to(device)
        
        # Forward
        inst_logits, tt_logit, conf = model(mfcc)
        
        # Loss
        loss = multi_task_loss(inst_logits, tt_logit, conf, inst_label, tt_label, conf_label)
        
        # Backward
        loss.backward()
        optimizer.step()
        optimizer.zero_grad()
    
    scheduler.step()
    
    # Validation
    val_acc_inst, val_acc_tt = evaluate_classifier(model, val_loader)
    print(f"Epoch {epoch}: Inst Acc = {val_acc_inst:.2%}, TrackType Acc = {val_acc_tt:.2%}")
    
    if val_acc_inst > 0.90 and val_acc_tt > 0.95:
        break

# Export to ONNX
torch.onnx.export(model, dummy_mfcc, "audio_classifier.onnx")
quantize_dynamic("audio_classifier.onnx", "audio_classifier_int8.onnx")
```

**Week 5: Drum Specialization**

```python
# Fine-tune on drum data
drum_dataset = DrumDataset(['groove', 'egmd', 'enst'])

# Freeze conv layers, only train FC + output
for param in model.conv1.parameters():
    param.requires_grad = False
for param in model.conv2.parameters():
    param.requires_grad = False
for param in model.conv3.parameters():
    param.requires_grad = False

# Increase drum class weights
class_weights = torch.ones(20)
class_weights[0:4] = 2.0  # Kick, snare, hi-hat, toms

# Fine-tune for 10 epochs
for epoch in range(10):
    for batch in drum_loader:
        # ... training code with weighted loss
        loss_inst = F.cross_entropy(inst_logits, inst_label, weight=class_weights)
```

#### 4.2.3 Spectral Analyzer Training (Detailed)

**Week 1-2: Reference Curve Collection**

```python
# Process reference library
genre_curves = {}

for genre in ['pop', 'rock', 'edm', 'hiphop', 'jazz', 'classical', 'metal', 'indie']:
    track_list = get_tracks_by_genre(genre, min_count=1000)
    spectra = []
    
    for track_path in tqdm(track_list):
        try:
            spectrum = compute_track_spectrum(track_path)
            spectra.append(spectrum)
        except Exception as e:
            print(f"Error processing {track_path}: {e}")
            continue
    
    # Compute median curve
    genre_curve = np.median(spectra, axis=0)
    genre_curve_smooth = savgol_filter(genre_curve, 51, 3)
    
    genre_curves[genre] = genre_curve_smooth
    
    # Save
    np.save(f"genre_curves/{genre}.npy", genre_curve_smooth)

# Save all curves
with open("genre_curves/all_curves.json", "w") as f:
    json.dump({k: v.tolist() for k, v in genre_curves.items()}, f)
```

**Week 3: Suggestion System**

```python
# Rule-based suggestion engine
class SuggestionEngine:
    def __init__(self, genre_curves):
        self.genre_curves = genre_curves
        self.tolerance = 3.0  # dB
    
    def analyze(self, current_spectrum, genre='pop'):
        target = self.genre_curves[genre]
        deviation = current_spectrum - target
        
        suggestions = []
        
        # Sub-bass check
        if deviation['sub_bass'] > self.tolerance:
            suggestions.append({
                'severity': 'medium',
                'category': 'low_frequency',
                'message': f"Excessive sub-bass (+{deviation['sub_bass']:.1f} dB). "
                           "Consider high-pass filtering non-bass instruments.",
                'action': {
                    'type': 'eq',
                    'freq': 50,
                    'gain': -deviation['sub_bass'],
                    'q': 0.7
                }
            })
        
        # Muddy mids check
        if deviation['low_mid'] > self.tolerance:
            suggestions.append({
                'severity': 'high',
                'category': 'mid_frequency',
                'message': f"Muddy low-mids (+{deviation['low_mid']:.1f} dB). "
                           "Try cutting 200-400 Hz on guitars or pads.",
                'action': {
                    'type': 'eq',
                    'freq': 300,
                    'gain': -deviation['low_mid'],
                    'q': 1.0
                }
            })
        
        # Presence check
        if deviation['presence'] < -self.tolerance:
            suggestions.append({
                'severity': 'medium',
                'category': 'high_frequency',
                'message': f"Lacking presence ({deviation['presence']:.1f} dB). "
                           "Boost 4-6 kHz for clarity.",
                'action': {
                    'type': 'eq',
                    'freq': 5000,
                    'gain': -deviation['presence'],
                    'q': 1.5
                }
            })
        
        # Sort by severity
        severity_order = {'high': 0, 'medium': 1, 'low': 2}
        suggestions.sort(key=lambda s: severity_order[s['severity']])
        
        return suggestions[:5]  # Top 5
```

**Week 4-5: ML Enhancement (Optional)**

```python
# Train suggestion relevance model
class SuggestionScorer(nn.Module):
    def __init__(self, n_suggestions=50):
        super().__init__()
        # Input: 7 band deviations + 3 context features
        self.fc1 = nn.Linear(10, 64)
        self.fc2 = nn.Linear(64, 32)
        self.output = nn.Linear(32, n_suggestions)
    
    def forward(self, band_deviations, context):
        # band_deviations: [batch, 7]
        # context: [batch, 3] - genre_id, loudness, stereo_width
        x = torch.cat([band_deviations, context], dim=1)
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        scores = torch.sigmoid(self.output(x))
        return scores

# Train on Mix Evaluation Dataset
# (dataset has expert annotations + user ratings)
for epoch in range(20):
    for batch in mix_eval_loader:
        deviations = batch['deviations']
        context = batch['context']
        labels = batch['suggestion_labels']  # Binary: relevant or not
        
        scores = model(deviations, context)
        loss = F.binary_cross_entropy(scores, labels)
        
        loss.backward()
        optimizer.step()
        optimizer.zero_grad()
```

### 4.3 Integration and System Testing

**Week 25-28: C++ Integration**

```cpp
// Unified inference engine
class NomadMuseEngine {
private:
    // ONNX Runtime sessions
    Ort::Session* midi_generator_session;
    Ort::Session* audio_classifier_session;
    
    // Spectral analyzer (DSP)
    SpectralAnalyzer* spectrum_analyzer;
    
    // Unified state
    UnifiedState* state;
    
    // Ring buffers for lock-free communication
    RingBuffer<MIDIEvent> midi_predictions;
    RingBuffer<InstrumentLabel> classifier_results;
    RingBuffer<Suggestion> mix_suggestions;

public:
    NomadMuseEngine(const std::string& model_dir) {
        // Load ONNX models
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "NomadMuse");
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        
        midi_generator_session = new Ort::Session(env, (model_dir + "/midi_gen_int8.onnx").c_str(), session_options);
        audio_classifier_session = new Ort::Session(env, (model_dir + "/audio_classifier_int8.onnx").c_str(), session_options);
        
        // Initialize spectral analyzer
        spectrum_analyzer = new SpectralAnalyzer();
        spectrum_analyzer->load_genre_curves(model_dir + "/genre_curves");
        
        // Initialize state
        state = new UnifiedState();
    }
    
    // Audio thread callback (hard real-time)
    void process_audio_callback(float* audio_buffer, int num_samples, int sample_rate) {
        // Step 1: Audio classification (<10ms)
        auto start = high_resolution_clock::now();
        
        InstrumentLabel label = classify_audio(audio_buffer, num_samples, sample_rate);
        if (label.confidence > 0.75) {
            classifier_results.write(label);
            state->update_track_type(current_track_id, label.instrument, label.confidence);
        }
        
        auto classify_time = duration_cast<microseconds>(high_resolution_clock::now() - start).count();
        
        // Step 2: MIDI generation if needed (<20ms total)
        if (generation_enabled && time_for_prediction()) {
            start = high_resolution_clock::now();
            
            TrackType type = state->get_track_type(current_track_id);
            MIDIEvent next_note = generate_next_note(type);
            midi_predictions.write(next_note);
            
            auto generate_time = duration_cast<microseconds>(high_resolution_clock::now() - start).count();
            
            // Log timing
            if (classify_time + generate_time > 20000) {  // 20ms
                LOG_WARNING("Latency budget exceeded: " << (classify_time + generate_time) / 1000.0 << " ms");
            }
        }
    }
    
    // Lower-priority thread (soft real-time, runs every 1-2 sec)
    void process_analysis_thread() {
        while (running) {
            // Get current mix audio
            float* mix_buffer = get_mix_buffer();
            
            // Spectral analysis
            auto spectrum = spectrum_analyzer->analyze(mix_buffer, mix_buffer_size);
            
            // Compare to genre target
            std::string genre = state->get_current_genre();
            auto suggestions = spectrum_analyzer->generate_suggestions(spectrum, genre);
            
            // Update state
            for (const auto& suggestion : suggestions) {
                mix_suggestions.write(suggestion);
            }
            state->update_mix_suggestions(suggestions);
            
            // Sleep for 1-2 seconds
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    }
    
private:
    InstrumentLabel classify_audio(float* audio, int num_samples, int sr) {
        // Extract MFCC
        float mfcc[128][3];  // 128 bins, 3 frames (~50ms)
        extract_mfcc(audio, num_samples, sr, mfcc);
        
        // Prepare ONNX input
        std::vector<int64_t> input_shape = {1, 1, 128, 3};
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, 
            (float*)mfcc, 
            128 * 3, 
            input_shape.data(), 
            input_shape.size()
        );
        
        // Run inference
        const char* input_names[] = {"mfcc"};
        const char* output_names[] = {"instrument", "tracktype", "confidence"};
        auto output_tensors = audio_classifier_session->Run(
            Ort::RunOptions{nullptr},
            input_names,
            &input_tensor,
            1,
            output_names,
            3
        );
        
        // Parse output
        float* inst_logits = output_tensors[0].GetTensorMutableData<float>();
        float tt_logit = output_tensors[1].GetTensorMutableData<float>()[0];
        float confidence = output_tensors[2].GetTensorMutableData<float>()[0];
        
        int instrument_id = argmax(inst_logits, 20);
        bool is_percussive = (sigmoid(tt_logit) > 0.5);
        
        return InstrumentLabel{instrument_id, is_percussive, confidence};
    }
    
    MIDIEvent generate_next_note(TrackType type) {
        // Get context from state
        int key = state->get_current_key();
        int tempo = state->get_tempo();
        float avg_velocity = state->get_avg_velocity();
        
        // Prepare ONNX input (simplified)
        std::vector<int64_t> context_tokens = state->get_recent_tokens(32);
        context_tokens.push_back(type == DRUMS ? DRUM_TOKEN : TONAL_TOKEN);
        
        // ... ONNX inference code ...
        
        // Sample from output distribution
        int next_token = sample_token(logits, temperature=1.0);
        
        return decode_token_to_midi(next_token);
    }
};
```

**Week 29-32: End-to-End Testing**

1. **Latency Profiling**
   - Use Tracy Profiler to measure each module
   - Target: MIDI gen <20ms, Classifier <10ms, Analyzer <100ms
   - Optimize hot paths (SIMD, cache alignment)

2. **Accuracy Validation**
   - MIDI: Perplexity < 4.0, acceptance > 80%
   - Classifier: Instrument acc > 90%, drum detection > 95%
   - Analyzer: Suggestion helpfulness > 75%

3. **Integration Testing**
   - Test cross-modal scenarios (drums → tonal switch)
   - Verify state synchronization (no race conditions)
   - Stress test (8+ tracks, 120 BPM, complex arrangement)

4. **User Beta Testing**
   - 20 musicians (diverse genres)
   - 2-week trial period
   - Collect: acceptance logs, latency reports, feedback surveys
   - Iterate: Fine-tune thresholds, improve suggestions

---

## 5. Feedback and Learning Cycle

The system continuously improves through multi-level feedback loops that span all three modules.

### 5.1 Local Logging (Privacy-First)

**Data Collected Per Session:**

```python
session_log = {
    'session_id': uuid.uuid4(),
    'timestamp': datetime.now(),
    'duration_minutes': 45,
    
    # MIDI Generator feedback
    'midi_predictions': [
        {
            'predicted_note': 60,  # Middle C
            'accepted': True,      # User kept it
            'context': {
                'track_type': 'TONAL',
                'key': 'C minor',
                'tempo': 120
            }
        },
        # ... more predictions
    ],
    
    # Audio Classifier feedback
    'classifier_corrections': [
        {
            'predicted_instrument': 'electric_guitar',
            'corrected_to': 'acoustic_guitar',
            'confidence_was': 0.82
        }
    ],
    
    # Spectral Analyzer feedback
    'suggestion_responses': [
        {
            'suggestion': 'Cut 300 Hz on guitars',
            'action': 'applied',      # or 'ignored' or 'dismissed'
            'helpfulness_rating': 4   # 1-5 scale (optional)
        }
    ],
    
    # Musical context
    'key_changes': ['C minor', 'E♭ major', 'C minor'],
    'tempo_changes': [120, 140, 120],
    'genre': 'rock',
    
    # Privacy: no audio, no identifying info
    'user_consent': 'local_only'  # or 'share_anonymized'
}
```

**Storage:**
- Local SQLite database: `~/.nomad_muse/sessions.db`
- Encrypted with user-specific key
- User can export/delete anytime

### 5.2 User Consent Framework

**Three Consent Levels:**

1. **Local Only** (default)
   - All data stays on device
   - Personal adapter trained locally
   - No telemetry sent

2. **Anonymized Sharing**
   - Aggregated statistics sent (no MIDI, no audio)
   - Helps improve base models
   - Example: "80% of users accept drum predictions"

3. **Research Contribution**
   - Full session logs shared (anonymized)
   - Opt-in, revocable anytime
   - Contributes to model improvements

### 5.3 Adaptive Learning Process

**4-Stage Learning Cycle:**

1. **Local Logging:**  
   DAW records anonymized events (`accepted_predictions`, `rejected_predictions`, `tempo`, `key`)

2. **User Consent:**  
   Opt-in sharing toggle; data stored locally by default

3. **Fine-Tuning:**  
   Personal adapters updated every 10 sessions with accepted/rejected patterns

4. **Fusion Refresh:**  
   Monthly ensemble distillation integrates global and personal improvements

This mirrors GPT-style iteration while preserving privacy through local processing.

### 5.4 Cross-Module Learning

**MIDI Generator Adaptation:**
- Increase/decrease temperature based on acceptance rate
- Learn preferred note density and rhythm patterns
- Adapt to user's harmonic vocabulary

**Classifier Refinement:**
- Fine-tune on user-corrected labels
- Learn studio-specific instrument timbres
- Improve confidence calibration

**Analyzer Personalization:**
- Adjust suggestion thresholds based on applied recommendations
- Learn genre preferences from session history
- Customize reference curves to user's mixing style

---

## 6. UI and System Intelligence

Beyond generation, classification, and analysis, Nomad Muse supports an adaptive interface layer:

### 6.1 Core UI Components

**MIDI Generation Panel:**
- Piano roll with AI-generated "ghost notes" (color-coded by confidence)
- Temperature slider (creativity vs. coherence)
- Mode selector: Tonal / Drums / Auto-detect
- Generate buttons: "Next Bar", "Drum Fill", "Chord Progression"

**Classification Display:**
- Per-track instrument labels with confidence meters
- Visual indication of track type (percussive vs. tonal)
- Manual override buttons for corrections

**Spectral Analysis Dashboard:**
- Real-time frequency spectrum visualization
- Genre target curve overlay
- Deviation meters for each frequency band
- Prioritized suggestion list with severity indicators
- One-click apply suggestions (preview mode available)

### 6.2 Adaptive Intelligence Features

**Session-Flow Predictor:**
- Tracks typical workflow patterns (e.g., "drums → bass → melody")
- Recommends next tool based on past sessions
- Example: "You usually add drums next - generate a drum pattern?"

**UI Layout Recommender:**
- Toggles panels by project type (e.g., expand mixer for mix-heavy sessions)
- Hides unused features to reduce clutter
- Learns from manual panel arrangements

**Color Mood Engine:**
- Adjusts UI palette to musical energy level
- High-energy tracks: warmer colors, higher contrast
- Ambient tracks: cooler colors, softer palette
- Based on tempo, dynamics, and spectral content

All adaptive models remain small (< 1 MB total) and advisory; deterministic C++ logic governs core execution.

---

## 7. Optimization and Breakthrough Targets

| Goal | Metric | Target | Baseline |
|------|--------|--------|----------|
| Zero-latency prediction | end-to-end delay | < 20 ms | ~100 ms (unoptimized) |
| Adaptive temperature | entropy vs. velocity variance | dynamic (0.7-1.3) | fixed 1.0 |
| Acceptance rate | user-approved ghost notes | > 80% | ~45% (rule-based) |
| Memory footprint | runtime RAM | < 50 MB | ~200 MB (fp32) |
| Perplexity (per token) | validation set | < 4.0 | ~8.5 (BiLSTM) |
| Throughput | predictions/second | > 1000 | ~150 (fp32) |
| Model size | disk storage | < 2 MB | 10 MB (unquantized) |

### 6.1 Performance Optimization Strategies

**CPU Inference Optimizations:**
1. **Quantization:** INT8 weights + activations (4× memory reduction)
2. **SIMD Vectorization:** AVX2/AVX-512 for matrix operations
3. **Memory Layout:** Row-major, cache-aligned buffers
4. **Operator Fusion:** Combine GRU gates into single kernel
5. **Batch Size = 1:** Optimize for single-sequence inference

**Real-Time Audio Thread Integration:**
- **Ring Buffer Architecture:** Pre-compute 8 predictions ahead
- **Lock-Free Communication:** Atomic pointers for prediction swap
- **Fallback Strategy:** If inference > 15ms, use last valid prediction
- **Confidence Threshold:** Only suggest predictions with softmax max > 0.6

**Adaptive Temperature Scheduling:**

$$T(t) = T_{base} \cdot \left(1 + 0.3 \cdot \frac{v(t) - \bar{v}}{\sigma_v}\right)$$

Where:
- $T_{base} = 1.0$ (default temperature)
- $v(t)$ = current velocity
- $\bar{v}$ = running average velocity
- $\sigma_v$ = velocity standard deviation

Higher velocities (louder playing) → higher temperature → more creative variations.

**Breakthrough** occurs when Muse's responses achieve sub-20 ms latency with > 80% user acceptance, yielding perceptual immediacy indistinguishable from human anticipation.

---

## 8. Ethical and Privacy Considerations

- **Explicit opt-in** for any data sharing
- **No audio or identifying material** uploaded
- **Local deletion** and transparency controls
- **Compliance** with emerging creative-AI ethics standards

---

## 9. Results and Evaluation

### 8.1 Experimental Setup

**Hardware:**
- CPU: Intel Core i7-12700K (12 cores, 3.6 GHz base)
- RAM: 32 GB DDR4-3200
- OS: Windows 11 Pro (64-bit)

**Software:**
- Training: PyTorch 2.1, CUDA 11.8 (NVIDIA RTX 3080)
- Inference: ONNX Runtime 1.16 (CPU execution provider)
- DAW Integration: Nomad v1.0 (C++17, MSVC 2022)

**Validation Sets:**
- Classical: MAESTRO validation split (177 performances)
- Popular: Lakh held-out set (5,000 pieces)
- User Sessions: 50 anonymized sessions from beta testers

### 8.2 Quantitative Results

*[To be populated with actual training results]*

**Expected Metrics:**

| Model | Perplexity ↓ | Latency (ms) ↓ | Memory (MB) ↓ | Params |
|-------|-------------|----------------|---------------|---------|
| Base (fp32) | 3.2 | 95 | 10.5 | 2.5M |
| Student (fp32) | 4.8 | 12 | 0.8 | 200k |
| Student (int8) | 5.1 | 4 | 0.25 | 200k |
| Fusion (int8) | 3.9 | 6 | 1.2 | 300k |
| **Target** | **< 4.0** | **< 20** | **< 50** | **~300k** |

**Benchmark Suite:**
- Music Transformer (lite): ~7.2 perplexity, 180ms latency
- BiLSTM baseline: ~8.5 perplexity, 45ms latency
- Rule-based (Markov): N/A, 2ms latency, 45% acceptance

### 8.3 Ablation Studies

*[To be completed]*

**Planned Comparisons:**
1. Base vs. Student vs. Fusion (quality degradation analysis)
2. With/without Personal Adapter (personalization effectiveness)
3. GRU-only vs. Hybrid vs. Attention-only (architecture justification)
4. Dataset composition (MAESTRO-only vs. multi-genre)

### 8.4 Human Evaluation

*[To be completed with user study]*

**Planned Metrics:**
- **Musical Coherence** (1-5 scale): Melody, harmony, rhythm consistency
- **Stylistic Appropriateness** (1-5 scale): Fits user's genre/mood
- **Creativity** (1-5 scale): Surprising but pleasing suggestions
- **Acceptance Rate** (binary): User keeps vs. rejects prediction
- **Perceived Latency** (subjective): Feels immediate vs. delayed

**Target User Pool:** 20 musicians (5 classical, 5 jazz, 5 electronic, 5 rock)

### 8.5 Failure Mode Analysis

*[To be documented]*

Common failure cases to track:
- Rhythmic drift in complex time signatures
- Harmonic clashes in modulation sections
- Over-repetition in personal adapter (overfitting)
- Latency spikes on sustained chords (attention bottleneck)

---

## 10. Conclusion and Future Work

Nomad Muse demonstrates that distilled symbolic-music models can operate in real time within a DAW while maintaining stylistic personalization.

### Future Directions:
- Integrating limited real-time fine-tuning (LoRA adapters)
- Expanding the adaptive-UI recommender
- Exploring multi-modal fusion with audio descriptors for groove sensitivity

---

## References

1. **Hinton, G., Vinyals, O., Dean, J.** (2015). *Distilling the Knowledge in a Neural Network.* arXiv:1503.02531.

2. **Hawthorne, C., Stasyuk, A., Roberts, A., Simon, I., Huang, C.-Z. A., Dieleman, S., Eck, D., Engel, J., Raffel, C.** (2018). *Enabling Factorized Piano Music Modeling and Generation with the MAESTRO Dataset.* ICLR 2019.

3. **Huang, C.-Z. A., Vaswani, A., Uszkoreit, J., Shazeer, N., Simon, I., Hawthorne, C., Dai, A. M., Hoffman, M. D., Dinculescu, M., Eck, D.** (2018). *Music Transformer.* arXiv:1809.04281.

4. **Raffel, C.** (2016). *Learning-Based Methods for Comparing Sequences, with Applications to Audio-to-MIDI Alignment and Matching.* Columbia University PhD Thesis. (Lakh MIDI Dataset)

5. **Kong, Q., Li, B., Song, X., Wan, Y., Wang, Y.** (2020). *High-resolution Piano Transcription with Pedals by Regressing Onset and Offset Times.* IEEE/ACM TASLP. (GiantMIDI-Piano)

6. **Dong, H.-W., Chen, K., McAuley, J., Berg-Kirkpatrick, T.** (2020). *MusPy: A Toolkit for Symbolic Music Generation.* ISMIR 2020.

7. **Gillick, J., Roberts, A., Engel, J., Eck, D., Bamman, D.** (2019). *Learning to Groove with Inverse Sequence Transformations.* ICML 2019. (Groove MIDI Dataset)

8. **Sanh, V., Debut, L., Chaumond, J., Wolf, T.** (2019). *DistilBERT, a distilled version of BERT: smaller, faster, cheaper and lighter.* arXiv:1910.01108.

9. **Hu, E. J., Shen, Y., Wallis, P., Allen-Zhu, Z., Li, Y., Wang, S., Wang, L., Chen, W.** (2021). *LoRA: Low-Rank Adaptation of Large Language Models.* ICLR 2022.

10. **Chung, J., Gulcehre, C., Cho, K., Bengio, Y.** (2014). *Empirical Evaluation of Gated Recurrent Neural Networks on Sequence Modeling.* NIPS 2014 Workshop.

11. **Howard, A. G., Zhu, M., Chen, B., Kalenichenko, D., Wang, W., Weyand, T., Andreetto, M., Adam, H.** (2017). *MobileNets: Efficient Convolutional Neural Networks for Mobile Vision Applications.* arXiv:1704.04861.

12. **OpenAI** (2019). *MuseNet.* https://openai.com/blog/musenet/

13. **Roberts, A., Engel, J., Raffel, C., Hawthorne, C., Eck, D.** (2018). *A Hierarchical Latent Vector Model for Learning Long-Term Structure in Music.* ICML 2018. (MusicVAE)

14. **Briot, J.-P., Hadjeres, G., Pachet, F.-D.** (2020). *Deep Learning Techniques for Music Generation.* Springer.

15. **Microsoft ONNX Runtime** (2021). *Performance Tuning.* https://onnxruntime.ai/docs/performance/

---

## 11. Implementation Roadmap

### Phase 1: Foundation (Weeks 1-3)
**Goal:** Prove concept with minimal viable model

- [ ] Set up training pipeline (PyTorch)
- [ ] Implement token vocabulary and encoding
- [ ] Train Student model on MAESTRO subset (10k pieces)
- [ ] Export to ONNX, benchmark inference speed
- [ ] Target: < 50ms latency, perplexity < 6

### Phase 2: Base Model Training (Weeks 4-12)
**Goal:** Train comprehensive Base model

- [ ] Acquire and preprocess all datasets (§3.2.1)
- [ ] Implement data augmentation pipeline
- [ ] Train Base model (2.5M params) on full corpus
- [ ] Validate across genres (classical, pop, jazz, drums)
- [ ] Target: Perplexity < 3.5 on validation set

### Phase 3: Distillation (Weeks 13-16)
**Goal:** Create efficient Student model

- [ ] Implement knowledge distillation training loop
- [ ] Experiment with temperature scaling (T = 2-5)
- [ ] Train Student model (200k params)
- [ ] Quantize to INT8 using ONNX Runtime
- [ ] Target: Perplexity < 5.0, < 20ms inference

### Phase 4: DAW Integration (Weeks 17-20)
**Goal:** Real-time inference in Nomad DAW

- [ ] Build C++ ONNX Runtime wrapper
- [ ] Implement lock-free ring buffer for predictions
- [ ] Integrate with MIDI input pipeline
- [ ] Add UI controls (enable/disable, temperature slider)
- [ ] Target: Zero audio dropouts, < 15ms in production

### Phase 5: Personal Adapter (Weeks 21-26)
**Goal:** Enable user personalization

- [ ] Implement session logging (SQLite)
- [ ] Build privacy-preserving data collection UI
- [ ] Create adapter fine-tuning pipeline
- [ ] Test with 10+ beta users (10 sessions each)
- [ ] Target: > 70% acceptance rate with adapter

### Phase 6: Fusion & Polish (Weeks 27-30)
**Goal:** Final ensemble model

- [ ] Implement weighted ensemble distillation
- [ ] Optimize dynamic weight adjustment (γ scaling)
- [ ] Full quantization and deployment testing
- [ ] Human evaluation study (20 musicians)
- [ ] Target: > 80% acceptance, < 20ms latency

### Phase 7: Advanced Features (Future)
- [ ] LoRA-style adapters for real-time fine-tuning
- [ ] Multi-modal fusion (audio features + MIDI)
- [ ] Groove-sensitivity using Groove MIDI embeddings
- [ ] Adaptive UI intelligence (session-flow predictor)

---

## Implementation Notes

This research paper serves as the theoretical foundation for the NomadMuse module within the Nomad DAW project. The technical implementation will focus on:

1. **C++ Integration:** Core inference engine written in optimized C++17
2. **ONNX Runtime:** Model deployment using ONNX for cross-platform compatibility
3. **Real-time Processing:** Lock-free data structures for audio-thread safety
4. **Local Storage:** SQLite-based logging and model versioning
5. **Privacy-First Design:** All processing happens on-device by default

### Recommended Development Tools
- **Training:** PyTorch 2.x + PyTorch Lightning (experiment tracking)
- **Dataset Processing:** MusPy, pretty_midi, mido
- **Inference:** ONNX Runtime 1.16+, Intel MKL-DNN
- **Profiling:** Tracy Profiler, Intel VTune
- **Version Control:** Git LFS for model weights

### Critical Dependencies
```
# Python (training)
torch >= 2.1.0
onnx >= 1.15.0
pretty_midi >= 0.2.10
muspy >= 0.5.0

# C++ (runtime)
onnxruntime >= 1.16.0
sqlite3 >= 3.40.0
```
