# NOMAD DAW Core Implementation Plan
**Version**: 1.0  
**Date**: November 28, 2025  
**Author**: Kilo Code (Architect Mode)

## 📋 Table of Contents

1. [Executive Summary](#executive-summary)
2. [Critical Issues - Immediate Priority](#critical-issues---immediate-priority)
3. [Performance Optimizations](#performance-optimizations)
4. [Architecture Improvements](#architecture-improvements)
5. [New Features & Functionality](#new-features--functionality)
6. [Development & Testing Improvements](#development--testing-improvements)
7. [Long-term Strategic Improvements](#long-term-strategic-improvements)
8. [Implementation Timeline](#implementation-timeline)
9. [Success Metrics & Validation](#success-metrics--validation)
10. [Risk Assessment & Mitigation](#risk-assessment--mitigation)

---

## Executive Summary

### Current State Analysis
NOMAD DAW has achieved significant milestones:
- ✅ **Audio Engine**: 90% complete with WASAPI integration and multi-threading
- ✅ **UI Framework**: 75% complete with custom NomadUI and OpenGL rendering
- ✅ **Core Infrastructure**: Platform abstraction, logging, profiling
- 🚧 **Basic DAW Features**: 40% complete (transport, file browser, basic playback)

### Critical Gaps Identified
1. **Code Maintainability**: main.cpp violates single responsibility (1508 lines)
2. **Core DAW Functionality**: Missing sample manipulation, project management
3. **User Experience**: No undo/redo, save/load capabilities
4. **Performance**: Audio thread efficiency, memory management
5. **Architecture**: Need foundation for plugin system, command pattern

### Implementation Strategy
**Phase-based approach** prioritizing:
1. **Stability** (refactoring, testing)
2. **Core Features** (sample manipulation, project management)
3. **Performance** (optimization, profiling)
4. **Advanced Features** (MIDI, plugins, effects)

---

## Critical Issues - Immediate Priority

### 1. Refactor Main.cpp (Priority: URGENT)

#### Current Problem

```cpp
// main.cpp - 1508 lines of monolithic code
class NomadApp {
    // 400+ lines of initialization
    // 300+ lines of event handling  
    // 200+ lines of audio callback
    // 600+ lines of various other responsibilities
};
```

#### Implementation Plan

**Step 1: Create AppController Class**

```cpp
// AppController.h
#pragma once
#include <memory>
#include "SystemInitializer.h"
#include "EventHandler.h"
#include "AudioController.h"
#include "UIController.h"

namespace Nomad {
class AppController {
public:
    AppController();
    ~AppController();
    
    bool initialize(int argc, char* argv[]);
    void run();
    void shutdown();
    
private:
    SystemInitializer m_systemInit;
    EventHandler m_eventHandler;
    AudioController m_audioController;
    UIController m_uiController;
    std::unique_ptr<AppState> m_state;
};

// AppController.cpp - ~100 lines
bool AppController::initialize(int argc, char* argv[]) {
    if (!m_systemInit.initialize()) return false;
    if (!m_audioController.initialize()) return false;
    if (!m_uiController.initialize()) return false;
    if (!m_eventHandler.initialize()) return false;
    return true;
}
```

**Step 2: Extract SystemInitializer**

```cpp
// SystemInitializer.h
class SystemInitializer {
public:
    bool initialize();
    void shutdown();
    
private:
    bool initializeLogging();
    bool initializePlatform();
    bool initializeAudio();
    bool initializeUI();
    bool initializeProfiler();
};

// SystemInitializer.cpp - ~150 lines
bool SystemInitializer::initialize() {
    if (!initializeLogging()) return false;
    if (!initializePlatform()) return false;
    if (!initializeAudio()) return false;
    if (!initializeUI()) return false;
    if (!initializeProfiler()) return false;
    return true;
}
```

**Step 3: Extract EventHandler**

```cpp
// EventHandler.h
class EventHandler {
public:
    bool initialize();
    void handleEvents();
    void setCallbacks(/* callback interfaces */);
    
private:
    void setupWindowCallbacks();
    void setupKeyCallbacks();
    void setupMouseCallbacks();
    void setupAudioCallbacks();
};

// EventHandler.cpp - ~200 lines
void EventHandler::setupKeyCallbacks() {
    m_window->setKeyCallback([this](int key, bool pressed) {
        if (key == NOMAD_KEY_ESCAPE && pressed) {
            m_appController->requestShutdown();
        }
        // ... other key handling
    });
}
```

**Step 4: Create AppState Management**

```cpp
// AppState.h
class AppState {
public:
    enum class State {
        Initializing,
        Running,
        Paused,
        ShuttingDown
    };
    
    void setState(State newState);
    State getState() const { return m_currentState; }
    bool isRunning() const { return m_currentState == State::Running; }
    
private:
    std::atomic<State> m_currentState{State::Initializing};
};
```

#### Code Migration Strategy
1. **Week 1**: Create new class structure, move 30% of main.cpp code
2. **Week 2**: Move audio handling, event handling (~50% total)
3. **Week 3**: Complete migration, remove main.cpp, test thoroughly
4. **Validation**: Ensure identical functionality, better maintainability

#### Expected Benefits

- **Maintainability**: 5x easier to debug and modify
- **Testability**: Individual components can be unit tested
- **Readability**: Each class <300 lines, single responsibility
- **Extensibility**: Easy to add new features without modifying core

---

### 2. Implement Sample Drag-and-Drop (Priority: CRITICAL)

#### Current State

```cpp
// TrackManager.cpp - No drag-drop support
void TrackManager::addTrack(const std::string& name) {
    // Only creates empty tracks
}
```

#### Implementation Plan

**Step 1: Create Drop Zone Interface**

```cpp
// TimelineDropZone.h
class TimelineDropZone : public NUIComponent {
public:
    void setOnSampleDropped(std::function<void(const SampleDropInfo&)> callback);
    void setTimelineBounds(const NUIRect& bounds);
    
private:
    void onMouseDrop(const MouseDropEvent& event) override;
    bool isPointInTimeline(const NomadUI::NUIPoint& point);
    
    std::function<void(const SampleDropInfo&)> m_onSampleDropped;
    NUIRect m_timelineBounds;
    bool m_dragActive = false;
};

// SampleDropInfo.h
struct SampleDropInfo {
    std::string filePath;
    NomadUI::NUIPoint dropPosition;
    double timelinePosition;
    int trackIndex;
    bool isValid = false;
};
```

**Step 2: Visual Feedback System**

```cpp
// DragDropVisualizer.h
class DragDropVisualizer {
public:
    void startDrag(const std::string& filePath);
    void updateDrag(const NomadUI::NUIPoint& position);
    void endDrag();
    void render(NUIRenderer& renderer);
    
private:
    std::string m_draggedFile;
    NomadUI::NUIPoint m_currentPosition;
    bool m_isDragging = false;
    float m_opacity = 1.0f;
};

// TimelineUI.h (enhancement)
class TimelineUI {
public:
    void setDragDropEnabled(bool enabled) { m_dragDropEnabled = enabled; }
    void setOnSampleDropped(std::function<void(const SampleDropInfo&)> callback);
    
private:
    bool m_dragDropEnabled = false;
    std::unique_ptr<TimelineDropZone> m_dropZone;
    std::unique_ptr<DragDropVisualizer> m_dragVisualizer;
};
```

**Step 3: File Format Validation**

```cpp
// AudioFileValidator.h
class AudioFileValidator {
public:
    static bool isValidAudioFile(const std::string& filePath);
    static AudioFileInfo getFileInfo(const std::string& filePath);
    static std::vector<std::string> getSupportedFormats();
    
private:
    static bool checkFileExtension(const std::string& filePath);
    static bool validateWavFile(const std::string& filePath);
    static bool validateMp3File(const std::string& filePath);
};

struct AudioFileInfo {
    std::string format;
    int sampleRate = 0;
    int channels = 0;
    double duration = 0.0;
    uint64_t fileSize = 0;
};
```

**Step 4: Timeline Integration**

```cpp
// Timeline.h (enhancement)
class Timeline {
public:
    void addSampleAtPosition(const std::string& filePath, double position, int trackIndex);
    void removeSampleAt(double position, int trackIndex);
    void moveSample(double fromPosition, double toPosition, int trackIndex);
    
    // Sample management
    std::vector<SampleClip*> getSamplesAt(double position, int trackIndex);
    void selectSample(SampleClip* sample);
    void deselectAllSamples();
    
private:
    std::vector<std::unique_ptr<SampleClip>> m_samples;
    double positionToTimeline(double pixelPosition);
    double timelineToPosition(double timelineSeconds);
};

// SampleClip.h
class SampleClip {
public:
    SampleClip(const std::string& filePath, double startTime, double duration);
    
    void render(NUIRenderer& renderer, const NUIRect& bounds);
    bool containsPoint(const NomadUI::NUIPoint& point);
    void setPosition(double position);
    void setDuration(double duration);
    
    // Getters
    double getPosition() const { return m_startTime; }
    double getDuration() const { return m_duration; }
    std::string getFilePath() const { return m_filePath; }
    
private:
    std::string m_filePath;
    double m_startTime;
    double m_duration;
    std::unique_ptr<AudioBuffer> m_audioBuffer;
    NomadUI::NUIColor m_color;
    bool m_selected = false;
};
```

#### UI Integration Steps

1. **Enable drop zones** on timeline tracks
2. **Visual feedback** during drag operations
3. **File validation** with error handling
4. **Sample positioning** with timeline snapping
5. **Audio loading** with progress indicators

#### Testing Strategy

```cpp
// DragDropTests.cpp
TEST_CASE("Sample drag and drop functionality") {
    auto timeline = std::make_unique<Timeline>();
    
    SECTION("Valid WAV file drop") {
        SampleDropInfo info;
        info.filePath = "test_440hz.wav";
        info.timelinePosition = 1.5;
        info.trackIndex = 0;
        
        timeline->handleSampleDropped(info);
        
        auto samples = timeline->getSamplesAt(1.5, 0);
        REQUIRE(samples.size() == 1);
        REQUIRE(samples[0]->getFilePath() == "test_440hz.wav");
    }
    
    SECTION("Invalid file format rejection") {
        SampleDropInfo info;
        info.filePath = "invalid.txt";
        info.timelinePosition = 1.5;
        info.trackIndex = 0;
        
        timeline->handleSampleDropped(info);
        
        auto samples = timeline->getSamplesAt(1.5, 0);
        REQUIRE(samples.empty());
    }
}
```

---

### 3. Project Save/Load System (Priority: HIGH)

#### Implementation Plan

**Step 1: Project Data Structures**

```cpp
// ProjectData.h
struct ProjectData {
    std::string name;
    std::string version = "1.0";
    double tempo = 120.0;
    int sampleRate = 48000;
    int bufferSize = 256;
    
    std::vector<TrackData> tracks;
    std::vector<MarkerData> markers;
    std::string filePath;
    
    // Metadata
    std::string createdBy;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point modifiedAt;
};

struct TrackData {
    std::string id;
    std::string name;
    NomadUI::NUIColor color = NomadUI::NUIColor::Gray;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    bool armed = false;
    
    std::vector<SampleClipData> clips;
};

struct SampleClipData {
    std::string filePath;
    double startTime;
    double duration;
    double offset;
    float gain = 1.0f;
    bool loop = false;
};

struct MarkerData {
    std::string name;
    double position;
    NomadUI::NUIColor color = NomadUI::NUIColor::Yellow;
};
```

**Step 2: JSON Serialization**

```cpp
// ProjectSerializer.h
class ProjectSerializer {
public:
    static bool saveProject(const ProjectData& project, const std::string& filePath);
    static bool loadProject(ProjectData& project, const std::string& filePath);
    static std::string serializeToString(const ProjectData& project);
    static bool deserializeFromString(ProjectData& project, const std::string& json);
    
private:
    static json serializeTrack(const TrackData& track);
    static TrackData deserializeTrack(const json& trackJson);
    static json serializeSampleClip(const SampleClipData& clip);
    static SampleClipData deserializeSampleClip(const json& clipJson);
};

// ProjectSerializer.cpp
bool ProjectSerializer::saveProject(const ProjectData& project, const std::string& filePath) {
    try {
        json projectJson;
        projectJson["name"] = project.name;
        projectJson["version"] = project.version;
        projectJson["tempo"] = project.tempo;
        projectJson["sampleRate"] = project.sampleRate;
        projectJson["bufferSize"] = project.bufferSize;
        
        // Serialize tracks
        for (const auto& track : project.tracks) {
            projectJson["tracks"].push_back(serializeTrack(track));
        }
        
        // Serialize markers
        for (const auto& marker : project.markers) {
            projectJson["markers"].push_back(marker);
        }
        
        // Write to file
        std::ofstream file(filePath);
        if (!file.is_open()) return false;
        
        file << projectJson.dump(4); // Pretty print with 4-space indent
        return true;
    }
    catch (const std::exception& e) {
        Log::error("Failed to save project: " + std::string(e.what()));
        return false;
    }
}
```

**Step 3: Project Manager Integration**

```cpp
// ProjectManager.h
class ProjectManager {
public:
    static ProjectManager& getInstance();
    
    bool createNewProject(const std::string& name);
    bool saveCurrentProject();
    bool saveProjectAs(const std::string& filePath);
    bool loadProject(const std::string& filePath);
    
    bool hasUnsavedChanges() const { return m_hasUnsavedChanges; }
    std::string getCurrentProjectPath() const { return m_currentProjectPath; }
    std::string getProjectName() const { return m_currentProjectName; }
    
    void setOnProjectChanged(std::function<void()> callback) {
        m_onProjectChanged = callback;
    }
    
private:
    ProjectManager() = default;
    
    bool serializeCurrentProject(ProjectData& data);
    bool deserializeToCurrentProject(const ProjectData& data);
    
    ProjectData m_currentProject;
    std::string m_currentProjectPath;
    std::string m_currentProjectName;
    std::atomic<bool> m_hasUnsavedChanges{false};
    std::function<void()> m_onProjectChanged;
};

// ProjectManager.cpp
bool ProjectManager::saveCurrentProject() {
    if (m_currentProjectPath.empty()) {
        return saveProjectAs(""); // Show save dialog
    }
    
    ProjectData projectData;
    if (!serializeCurrentProject(projectData)) {
        return false;
    }
    
    if (ProjectSerializer::saveProject(projectData, m_currentProjectPath)) {
        m_hasUnsavedChanges.store(false);
        Log::info("Project saved: " + m_currentProjectPath);
        return true;
    }
    
    return false;
}
```

**Step 4: Auto-Save Functionality**

```cpp
// AutoSaveManager.h
class AutoSaveManager {
public:
    AutoSaveManager(std::chrono::minutes interval = std::chrono::minutes(5));
    
    void start();
    void stop();
    void markDirty(); // Call when project changes
    
private:
    void performAutoSave();
    
    std::chrono::minutes m_interval;
    std::thread m_autoSaveThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_dirty{false};
    std::mutex m_dirtyMutex;
};

// AutoSaveManager.cpp
void AutoSaveManager::markDirty() {
    std::lock_guard<std::mutex> lock(m_dirtyMutex);
    m_dirty.store(true);
}

void AutoSaveManager::performAutoSave() {
    while (m_running.load()) {
        std::this_thread::sleep_for(m_interval);
        
        if (m_dirty.load()) {
            auto& projectManager = ProjectManager::getInstance();
            if (projectManager.hasUnsavedChanges() && !projectManager.getCurrentProjectPath().empty()) {
                // Save to backup file
                std::string backupPath = projectManager.getCurrentProjectPath() + ".autosave";
                ProjectSerializer::saveProject(m_currentProject, backupPath);
                
                m_dirty.store(false);
                Log::debug("Auto-save completed");
            }
        }
    }
}
```

**Step 5: UI Integration**

```cpp
// FileMenu.h (new)
class FileMenu : public NUIComponent {
public:
    void setOnNewProject(std::function<void()> callback);
    void setOnOpenProject(std::function<void()> callback);
    void setOnSaveProject(std::function<void()> callback);
    void setOnSaveAsProject(std::function<void()> callback);
    
private:
    void renderMenu(NUIRenderer& renderer) override;
    
    std::function<void()> m_onNewProject;
    std::function<void()> m_onOpenProject;
    std::function<void()> m_onSaveProject;
    std::function<void()> m_onSaveAsProject;
};

// Integration in main application
void NomadContent::initializeProjectManagement() {
    auto& projectManager = ProjectManager::getInstance();
    
    projectManager.setOnProjectChanged([this]() {
        m_fileMenu->updateSaveState(projectManager.hasUnsavedChanges());
        m_titleBar->updateTitle(projectManager.getProjectName(), 
                               projectManager.hasUnsavedChanges());
    });
}
```

#### File Format Specification

```json
{
    "name": "My First Project",
    "version": "1.0",
    "tempo": 120.0,
    "sampleRate": 48000,
    "bufferSize": 256,
    "tracks": [
        {
            "id": "track_001",
            "name": "Drums",
            "color": [255, 100, 100],
            "volume": 0.8,
            "pan": 0.0,
            "muted": false,
            "solo": false,
            "clips": [
                {
                    "filePath": "samples/kick.wav",
                    "startTime": 0.0,
                    "duration": 2.5,
                    "offset": 0.0,
                    "gain": 1.0
                }
            ]
        }
    ],
    "markers": [
        {
            "name": "Verse",
            "position": 4.0,
            "color": [255, 255, 0]
        }
    ]
}
```

---

### 4. Undo/Redo System (Priority: HIGH)

#### Implementation Plan

**Step 1: Command Pattern Foundation**

```cpp
// Command.h
class Command {
public:
    virtual ~Command() = default;
    virtual bool execute() = 0;
    virtual bool undo() = 0;
    virtual std::string getDescription() const = 0;
    virtual bool canMergeWith(const Command* other) const { return false; }
};

// CommandManager.h
class CommandManager {
public:
    static CommandManager& getInstance();
    
    void executeCommand(std::unique_ptr<Command> command);
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
    void undo();
    void redo();
    void clearHistory();
    
    size_t getUndoStackSize() const { return m_undoStack.size(); }
    size_t getRedoStackSize() const { return m_redoStack.size(); }
    
private:
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
    static constexpr size_t MAX_HISTORY_SIZE = 100;
};
```

**Step 2: Concrete Command Implementations**

```cpp
// AddTrackCommand.h
class AddTrackCommand : public Command {
public:
    AddTrackCommand(TrackManager* trackManager, const std::string& trackName);
    
    bool execute() override;
    bool undo() override;
    std::string getDescription() const override { return "Add Track"; }
    
private:
    TrackManager* m_trackManager;
    std::string m_trackName;
    std::shared_ptr<Track> m_addedTrack;
    size_t m_trackIndex;
};

bool AddTrackCommand::execute() {
    m_addedTrack = m_trackManager->addTrack(m_trackName);
    m_trackIndex = m_trackManager->getTrackCount() - 1;
    return m_addedTrack != nullptr;
}

bool AddTrackCommand::undo() {
    if (m_addedTrack) {
        m_trackManager->removeTrack(m_trackIndex);
        return true;
    }
    return false;
}

// AddSampleClipCommand.h
class AddSampleClipCommand : public Command {
public:
    AddSampleClipCommand(Timeline* timeline, const std::string& filePath, 
                        double position, int trackIndex);
    
    bool execute() override;
    bool undo() override;
    std::string getDescription() const override { return "Add Sample"; }
    bool canMergeWith(const Command* other) const override;
    
private:
    Timeline* m_timeline;
    std::string m_filePath;
    double m_position;
    int m_trackIndex;
    std::unique_ptr<SampleClip> m_addedClip;
};

bool AddSampleClipCommand::execute() {
    auto clip = std::make_unique<SampleClip>(m_filePath, m_position, 0.0);
    if (clip->loadAudioFile()) {
        m_timeline->addSampleClip(std::move(clip), m_trackIndex);
        return true;
    }
    return false;
}
```

**Step 3: Macro Commands**

```cpp
// MacroCommand.h
class MacroCommand : public Command {
public:
    void addCommand(std::unique_ptr<Command> command);
    bool execute() override;
    bool undo() override;
    std::string getDescription() const override;
    
private:
    std::vector<std::unique_ptr<Command>> m_commands;
};

bool MacroCommand::execute() {
    // Execute all commands
    for (auto& command : m_commands) {
        if (!command->execute()) {
            // Undo all previously executed commands on failure
            for (auto it = m_commands.begin(); it != &command; ++it) {
                (*it)->undo();
            }
            return false;
        }
    }
    return true;
}

bool MacroCommand::undo() {
    // Undo in reverse order
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        (*it)->undo();
    }
    return true;
}
```

**Step 4: Timeline Integration**

```cpp
// Timeline.h (enhancement)
class Timeline {
public:
    // Command-based operations
    void addSampleClipCommand(const std::string& filePath, double position, int trackIndex);
    void moveSampleClipCommand(SampleClip* clip, double newPosition);
    void deleteSampleClipCommand(SampleClip* clip);
    void splitSampleClipCommand(SampleClip* clip, double splitPosition);
    
private:
    void executeCommand(std::unique_ptr<Command> command);
    
    CommandManager& m_commandManager = CommandManager::getInstance();
};

void Timeline::addSampleClipCommand(const std::string& filePath, double position, int trackIndex) {
    auto command = std::make_unique<AddSampleClipCommand>(this, filePath, position, trackIndex);
    m_commandManager.executeCommand(std::move(command));
## Performance Optimizations

### 1. Audio Thread Pool Optimization

#### Current Analysis
```cpp
// TrackManager.cpp - Current implementation has potential issues
void AudioThreadPool::workerThread() {
    while (true) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_condition.wait(lock, [this] { 
            return m_stop || !m_tasks.empty(); 
        });
        // Potential priority inversion
    }
}
```

#### Implementation Plan

**Step 1: Lock-Free Queue Implementation**

```cpp
// LockFreeQueue.h
template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue(size_t capacity);
    ~LockFreeQueue();
    
    bool tryPush(T&& value);
    bool tryPop(T& value);
    size_t size() const;
    bool empty() const;
    
private:
    struct Node {
        std::atomic<T*> data{nullptr};
        std::atomic<Node*> next{nullptr};
    };
    
    std::atomic<Node*> m_head;
    std::atomic<Node*> m_tail;
    std::atomic<size_t> m_size;
    
    Node* m_nodePool;
    std::atomic<size_t> m_poolIndex;
};

// AudioTaskQueue.h
class AudioTaskQueue {
public:
    void submit(std::function<void()>&& task);
    bool tryExecuteOne();
    size_t pendingTasks() const;
    
private:
    LockFreeQueue<std::function<void()>> m_queue;
    std::atomic<bool> m_hasWork{false};
};
```

**Step 2: Real-Time Priority Thread Management**

```cpp
// RealtimeThreadPool.h
class RealtimeThreadPool {
public:
    RealtimeThreadPool(size_t numThreads);
    ~RealtimeThreadPool();
    
    void submitRealtimeTask(std::function<void()>&& task);
    void submitBackgroundTask(std::function<void()>&& task);
    
private:
    struct ThreadInfo {
        std::thread thread;
        std::atomic<bool> running{false};
        int priority = 0;
        std::chrono::steady_clock::time_point lastActivity;
    };
    
    void setupRealtimePriority(std::thread& thread, int priority);
    void workerThread(size_t threadId);
    
    std::vector<ThreadInfo> m_threads;
    AudioTaskQueue m_realtimeQueue;
    TaskQueue m_backgroundQueue;
};

void RealtimeThreadPool::setupRealtimePriority(std::thread& thread, int priority) {
#ifdef _WIN32
    HANDLE nativeHandle = thread.native_handle();
    SetThreadPriority(nativeHandle, THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadPriorityBoost(nativeHandle, FALSE);
    
    // Set processor affinity to specific cores
    DWORD_PTR processorMask = 1ULL << priority;
    SetThreadAffinityMask(nativeHandle, processorMask);
    
#elif defined(__linux__)
    pthread_t nativeHandle = thread.native_handle();
    
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(nativeHandle, SCHED_FIFO, &param);
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(priority % std::thread::hardware_concurrency(), &cpuset);
    pthread_setaffinity_np(nativeHandle, sizeof(cpu_set_t), &cpuset);
#endif
}
```

**Step 3: Work Stealing Algorithm**

```cpp
// WorkStealingQueue.h
class WorkStealingQueue {
public:
    void push(std::function<void()>&& task);
    bool trySteal(std::function<void()>&& task);
    bool tryPop(std::function<void()>&& task);
    
private:
    std::deque<std::function<void()>> m_localQueue;
    std::vector<std::deque<std::function<void()>>*> m_queues;
    std::mutex m_stealMutex;
};

void WorkStealingQueue::trySteal(std::function<void()>&& task) {
    std::lock_guard<std::mutex> lock(m_stealMutex);
    
    // Try to steal from other queues (round-robin)
    for (auto queue : m_queues) {
        if (!queue->empty()) {
            task = std::move(queue->front());
            queue->pop_front();
            return true;
        }
    }
    return false;
}
```

**Step 4: Memory Pool for Audio Buffers**

```cpp
// AudioBufferPool.h
class AudioBufferPool {
public:
    AudioBufferPool(size_t bufferSize, size_t poolSize);
    ~AudioBufferPool();
    
    float* acquireBuffer();
    void releaseBuffer(float* buffer);
    void reset();
    
private:
    struct BufferNode {
        std::array<float, MAX_BUFFER_SIZE> data;
        std::atomic<bool> inUse{false};
    };
    
    std::vector<std::unique_ptr<BufferNode>> m_buffers;
    std::stack<float*> m_availableBuffers;
    std::mutex m_poolMutex;
};

// Integration in TrackManager
void TrackManager::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime) {
    auto bufferPool = AudioBufferPool::getInstance();
    
    // Use pool for per-track processing
    for (auto& track : m_tracks) {
        float* trackBuffer = bufferPool->acquireBuffer();
        track->processAudio(trackBuffer, numFrames, streamTime);
        
        // Mix into output
        for (uint32_t i = 0; i < numFrames * 2; ++i) {
            outputBuffer[i] += trackBuffer[i];
        }
        
        bufferPool->releaseBuffer(trackBuffer);
    }
}
```

#### Performance Metrics

```cpp
// AudioPerformanceMetrics.h
struct AudioPerformanceMetrics {
    std::atomic<double> avgProcessingTime{0.0};
    std::atomic<double> maxProcessingTime{0.0};
    std::atomic<uint64_t> totalProcessedBlocks{0};
    std::atomic<uint32_t> xruns{0};
    std::atomic<bool> realtimeViolation{false};
};

class AudioPerformanceMonitor {
public:
    void recordProcessingTime(double timeMs);
    void recordXrun();
    void reset();
    
    AudioPerformanceMetrics getMetrics() const;
    
private:
    AudioPerformanceMetrics m_metrics;
    std::chrono::steady_clock::time_point m_lastReset;
};
```

---

### 2. UI Rendering Pipeline Optimization

#### Current Analysis

```cpp
// NUIRendererGL.cpp - Potential inefficiencies
void NUIRendererGL::render() {
    // Re-renders entire UI every frame
    for (auto& component : m_components) {
        component->render(*this); // No dirty region tracking
    }
}
```

#### Implementation Plan

**Step 1: Dirty Region Tracking**

```cpp
// DirtyRegionTracker.h
class DirtyRegionTracker {
public:
    void markDirty(const NUIRect& region);
    void markDirty(int x, int y, int width, int height);
    std::vector<NUIRect> getDirtyRegions() const;
    void clear();
    bool hasDirtyRegions() const;
    
private:
    std::vector<NUIRect> m_dirtyRegions;
    std::mutex m_regionsMutex;
    
    void mergeOverlappingRegions();
};

// Enhanced NUIRenderer
class OptimizedNUIRenderer : public NUIRenderer {
public:
    void render() override;
    void markRegionDirty(const NUIRect& region) override;
    void setBatchingEnabled(bool enabled) override { m_batchingEnabled = enabled; }
    
private:
    void renderDirtyRegions();
    void batchRenderComponents(const std::vector<NUIRect>& regions);
    
    DirtyRegionTracker m_dirtyTracker;
    bool m_batchingEnabled = true;
    std::vector<NUIComponent*> m_culledComponents;
};

void OptimizedNUIRenderer::render() {
    if (m_dirtyTracker.hasDirtyRegions()) {
        auto dirtyRegions = m_dirtyTracker.getDirtyRegions();
        
        if (m_batchingEnabled) {
            batchRenderComponents(dirtyRegions);
        } else {
            renderDirtyRegions();
        }
        
        m_dirtyTracker.clear();
    }
}
```

**Step 2: Component Culling System**

```cpp
// ComponentCuller.h
class ComponentCuller {
public:
    std::vector<NUIComponent*> cullInvisible(const std::vector<NUIComponent*>& components);
    std::vector<NUIComponent*> cullByRegion(const std::vector<NUIComponent*>& components, 
                                           const NUIRect& region);
    
private:
    bool isComponentVisible(NUIComponent* component);
    bool isComponentInRegion(NUIComponent* component, const NUIRect& region);
};

std::vector<NUIComponent*> ComponentCuller::cullInvisible(const std::vector<NUIComponent*>& components) {
    std::vector<NUIComponent*> visibleComponents;
    
    for (auto component : components) {
        if (isComponentVisible(component)) {
            visibleComponents.push_back(component);
        }
    }
    
    return visibleComponents;
}
```

**Step 3: Render Batching System**

```cpp
// RenderBatch.h
class RenderBatch {
public:
    void addRect(const NUIRect& rect, const NUIColor& color, float radius = 0.0f);
    void addText(const std::string& text, const NomadUI::NUIPoint& position, 
                const NomadUI::NUIFont& font, const NUIColor& color);
    void addLine(const NomadUI::NUIPoint& start, const NomadUI::NUIPoint& end, 
                const NUIColor& color, float width = 1.0f);
    
    void execute(NUIRenderer& renderer);
    void clear();
    
private:
    struct RectDraw {
        NUIRect bounds;
        NUIColor color;
        float radius;
    };
    
    std::vector<RectDraw> m_rects;
    // ... other draw commands
};

void RenderBatch::execute(NUIRenderer& renderer) {
    // Batch all rectangle draws
    if (!m_rects.empty()) {
        renderer.beginBatch();
        
        for (const auto& rect : m_rects) {
            renderer.fillRect(rect.bounds, rect.color, rect.radius);
        }
        
        renderer.endBatch();
    }
}
```

**Step 4: Texture Atlas System**

```cpp
// TextureAtlas.h
class TextureAtlas {
public:
    struct TextureRegion {
        uint32_t x, y, width, height;
        uint32_t textureId;
    };
    
    uint32_t addTexture(const std::string& name, const std::vector<uint8_t>& data, 
                       int width, int height);
    TextureRegion getRegion(const std::string& name) const;
    void bindAtlas();
    
private:
    struct AtlasEntry {
        TextureRegion region;
        std::string name;
    };
    
    std::unordered_map<std::string, AtlasEntry> m_textures;
    uint32_t m_atlasTextureId = 0;
    int m_atlasWidth = 0, m_atlasHeight = 0;
    
    void packTextures();
    void uploadAtlas();
};

// IconCache.h
class IconCache {
public:
    static IconCache& getInstance();
    
    uint32_t getIconId(const std::string& iconName);
    void preloadIcons(const std::vector<std::string>& iconNames);
    
private:
    TextureAtlas m_atlas;
    std::unordered_map<std::string, uint32_t> m_iconCache;
};
```

**Step 5: Adaptive Quality System**

```cpp
// AdaptiveQualityManager.h
class AdaptiveQualityManager {
public:
    enum class QualityLevel {
        High,    // Full effects, high FPS
        Medium,  // Some effects, balanced
        Low      // Minimal effects, high FPS
    };
    
    void setQualityLevel(QualityLevel level);
    QualityLevel getQualityLevel() const { return m_currentLevel; }
    void adaptBasedOnPerformance(double frameTime);
    
private:
    QualityLevel m_currentLevel = QualityLevel::High;
    std::chrono::steady_clock::time_point m_lastAdaptation;
    std::deque<double> m_frameTimeHistory;
    
    void applyQualitySettings(QualityLevel level);
};

void AdaptiveQualityManager::adaptBasedOnPerformance(double frameTime) {
    m_frameTimeHistory.push_back(frameTime);
    if (m_frameTimeHistory.size() > 60) { // 1 second at 60fps
        m_frameTimeHistory.pop_front();
    }
    
    // Calculate average frame time
    double avgFrameTime = 0.0;
    for (auto time : m_frameTimeHistory) {
        avgFrameTime += time;
    }
    avgFrameTime /= m_frameTimeHistory.size();
    
    // Adapt quality based on performance
    if (avgFrameTime > 20.0) { // >20ms (50fps)
        if (m_currentLevel == QualityLevel::High) {
            setQualityLevel(QualityLevel::Medium);
        } else if (m_currentLevel == QualityLevel::Medium) {
            setQualityLevel(QualityLevel::Low);
        }
    } else if (avgFrameTime < 12.0) { // <12ms (83fps)
        if (m_currentLevel == QualityLevel::Low) {
            setQualityLevel(QualityLevel::Medium);
        } else if (m_currentLevel == QualityLevel::Medium) {
            setQualityLevel(QualityLevel::High);
        }
    }
}
```

---

## Architecture Improvements

### 1. Configuration Management System

#### Current Problem

```cpp
// Scattered hard-coded values throughout codebase
const int DEFAULT_BUFFER_SIZE = 256;  // main.cpp
const float DEFAULT_VOLUME = 1.0f;    // Track.cpp
const double DEFAULT_TEMPO = 120.0;   // TransportBar.cpp
```

#### Implementation Plan

**Step 1: Configuration Schema**

```cpp
// ConfigSchema.h
namespace Config {
    namespace Audio {
        constexpr int DEFAULT_SAMPLE_RATE = 48000;
        constexpr int DEFAULT_BUFFER_SIZE = 256;
        constexpr int MIN_BUFFER_SIZE = 64;
        constexpr int MAX_BUFFER_SIZE = 1024;
        constexpr float DEFAULT_VOLUME = 1.0f;
        constexpr float MIN_VOLUME = 0.0f;
        constexpr float MAX_VOLUME = 2.0f;
    }
    
    namespace UI {
        constexpr int DEFAULT_WINDOW_WIDTH = 1280;
        constexpr int DEFAULT_WINDOW_HEIGHT = 720;
        constexpr float MIN_WINDOW_WIDTH = 800.0f;
        constexpr float MIN_WINDOW_HEIGHT = 600.0f;
        constexpr double THEME_TRANSITION_TIME = 0.3;
        constexpr bool DEFAULT_FULLSCREEN = false;
    }
    
    namespace Performance {
        constexpr int MAX_AUDIO_THREADS = 16;
        constexpr int MIN_AUDIO_THREADS = 1;
        constexpr double PERFORMANCE_SAMPLE_COUNT = 10.0;
        constexpr double IDLE_TIMEOUT = 2.0;
        constexpr double PERFORMANCE_THRESHOLD = 0.018;
    }
}

// ConfigValue.h
template<typename T>
class ConfigValue {
public:
    ConfigValue(const T& defaultValue) : m_defaultValue(defaultValue), m_currentValue(defaultValue) {}
    
    const T& get() const { return m_currentValue; }
    void set(const T& value) { m_currentValue = value; }
    void reset() { m_currentValue = m_defaultValue; }
    
    operator const T&() const { return get(); }
    
private:
    T m_defaultValue;
    T m_currentValue;
};

// Global configuration instance
class AppConfig {
public:
    static AppConfig& getInstance();
    
    // Audio configuration
    ConfigValue<int>& sampleRate() { return m_sampleRate; }
    ConfigValue<int>& bufferSize() { return m_bufferSize; }
    ConfigValue<float>& masterVolume() { return m_masterVolume; }
    
    // UI configuration
    ConfigValue<int>& windowWidth() { return m_windowWidth; }
    ConfigValue<int>& windowHeight() { return m_windowHeight; }
    ConfigValue<bool>& fullscreen() { return m_fullscreen; }
    
    // Performance configuration
    ConfigValue<int>& audioThreads() { return m_audioThreads; }
    ConfigValue<bool>& vsyncEnabled() { return m_vsyncEnabled; }
    
    // Theme configuration
    std::string& themeName() { return m_themeName; }
    
    // File paths
    std::string& lastProjectPath() { return m_lastProjectPath; }
    std::string& samplesDirectory() { return m_samplesDirectory; }
    
private:
    AppConfig() = default;
    
    // Audio settings
    ConfigValue<int> m_sampleRate{Config::Audio::DEFAULT_SAMPLE_RATE};
    ConfigValue<int> m_bufferSize{Config::Audio::DEFAULT_BUFFER_SIZE};
    ConfigValue<float> m_masterVolume{Config::Audio::DEFAULT_VOLUME};
    
    // UI settings
    ConfigValue<int> m_windowWidth{Config::UI::DEFAULT_WINDOW_WIDTH};
    ConfigValue<int> m_windowHeight{Config::UI::DEFAULT_WINDOW_HEIGHT};
    ConfigValue<bool> m_fullscreen{Config::UI::DEFAULT_FULLSCREEN};
    
    // Performance settings
    ConfigValue<int> m_audioThreads{std::thread::hardware_concurrency()};
    ConfigValue<bool> m_vsyncEnabled{true};
    
    // Theme settings
    std::string m_themeName = "nomad-dark";
    
    // File paths
    std::string m_lastProjectPath;
    std::string m_samplesDirectory;
};
```

**Step 2: Configuration File Management**

```cpp
// ConfigManager.h
class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    bool loadFromFile(const std::string& filePath = "");
    bool saveToFile(const std::string& filePath = "");
    void resetToDefaults();
    
    // Hot reload support
    void watchConfigFile();
    void setOnConfigChanged(std::function<void(const std::string& key)> callback);
    
private:
    void applyConfigValue(const std::string& key, const nlohmann::json& value);
    std::string getDefaultConfigPath() const;
    
    std::string m_configFilePath;
    std::function<void(const std::string&)> m_onConfigChanged;
    std::chrono::steady_clock::time_point m_lastModified;
    
    static constexpr auto DEFAULT_CONFIG_FILE = "config/nomad_config.json";
};

// ConfigManager.cpp
bool ConfigManager::loadFromFile(const std::string& filePath) {
    std::string configPath = filePath.empty() ? getDefaultConfigPath() : filePath;
    
    try {
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            Log::warning("Config file not found, using defaults: " + configPath);
            return false;
        }
        
        nlohmann::json config;
        configFile >> config;
        configFile.close();
        
        // Apply audio settings
        if (config.contains("audio")) {
            auto& audio = config["audio"];
            if (audio.contains("sampleRate")) {
## New Features & Functionality

### 1. Volume/Pan Controls Per Track (Priority: HIGH)

#### Implementation Plan

**Step 1: Mixer Controls UI**
```cpp
// MixerStrip.h
class MixerStrip : public NUIComponent {
public:
    MixerStrip(std::shared_ptr<Track> track);
    
    void render(NUIRenderer& renderer) override;
    void onMouseClick(const MouseClickEvent& event) override;
    
    // Control callbacks
    void setOnVolumeChanged(std::function<void(float)> callback) { m_onVolumeChanged = callback; }
    void setOnPanChanged(std::function<void(float)> callback) { m_onPanChanged = callback; }
    void setOnMuteClicked(std::function<void(bool)> callback) { m_onMuteClicked = callback; }
    void setOnSoloClicked(std::function<void(bool)> callback) { m_onSoloClicked = callback; }
    
private:
    void renderVolumeSlider(NUIRenderer& renderer, const NUIRect& bounds);
    void renderPanKnob(NUIRenderer& renderer, const NUIRect& bounds);
    void renderMuteButton(NUIRenderer& renderer, const NUIRect& bounds);
    void renderSoloButton(NUIRenderer& renderer, const NUIRect& bounds);
    
    std::shared_ptr<Track> m_track;
    float m_volume = 1.0f;
    float m_pan = 0.0f;
    bool m_muted = false;
    bool m_soloed = false;
    
    std::function<void(float)> m_onVolumeChanged;
    std::function<void(float)> m_onPanChanged;
    std::function<void(bool)> m_onMuteClicked;
    std::function<void(bool)> m_onSoloClicked;
};

// MixerView.h
class MixerView : public NUIComponent {
public:
    MixerView(std::shared_ptr<TrackManager> trackManager);
    
    void render(NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    
    void addTrackMixer(std::shared_ptr<Track> track);
    void removeTrackMixer(const std::string& trackId);
    
private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::unique_ptr<MixerStrip>> m_mixerStrips;
    std::unordered_map<std::string, MixerStrip*> m_trackToMixer;
};
```

**Step 2: Audio Routing Enhancements**

```cpp
// Track.h (enhancement)
class Track {
public:
    void setVolume(float volume);
    void setPan(float pan);
    void setMuted(bool muted);
    void setSolo(bool soloed);
    
    float getVolume() const { return m_volume; }
    float getPan() const { return m_pan; }
    bool isMuted() const { return m_muted; }
    bool isSoloed() const { return m_soloed; }
    
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double sampleRate) override;
    
private:
    float m_volume = 1.0f;
    float m_pan = 0.0f;
    bool m_muted = false;
    bool m_soloed = false;
    
    void applyVolumeAndPan(float* buffer, uint32_t numFrames);
};

// TrackManager.h (enhancement)
class TrackManager {
public:
    bool hasAnySoloedTracks() const;
    bool shouldProcessTrack(const Track& track) const;
    
private:
    mutable std::mutex m_soloMutex;
    std::unordered_set<std::string> m_soloedTrackIds;
};

bool TrackManager::shouldProcessTrack(const Track& track) const {
    // If any tracks are soloed, only process soloed tracks
    if (hasAnySoloedTracks()) {
        return track.isSoloed();
    }
    
    // If no soloed tracks, process all non-muted tracks
    return !track.isMuted();
}

void TrackManager::processAudioSingleThreaded(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate) {
    // Check if any track is soloed (for exclusive solo behavior)
    bool anySoloed = false;
    for (const auto& track : m_tracks) {
        if (track && track->isSoloed()) {
            anySoloed = true;
            break;
        }
    }
    
    // Process each track - tracks will mix themselves into the output buffer
    for (const auto& track : m_tracks) {
        if (track && track->isPlaying()) {
            bool isSystemTrack = track->isSystemTrack();
            
            // If any track is soloed, only process soloed tracks (unless track is muted)
            // Exception: System tracks always play
            if (anySoloed && !track->isSoloed() && !isSystemTrack) {
                continue; // Skip non-soloed tracks when solo is active
            }
            
            track->processAudio(outputBuffer, numFrames, streamTime, outputSampleRate);
        }
    }
}
```

**Step 3: UI Integration**
```cpp
// MainWindow.h (enhancement)
class MainWindow {
public:
    void showMixerView(bool show);
    void toggleMixerView();
    
private:
    std::unique_ptr<MixerView> m_mixerView;
    bool m_mixerVisible = false;
};

// TransportBar.h (enhancement)
class TransportBar {
public:
    void setOnMixerToggle(std::function<void()> callback) { m_onMixerToggle = callback; }
    
    void render(NUIRenderer& renderer) override;
    void onMouseClick(const MouseClickEvent& event) override;
    
private:
    std::function<void()> m_onMixerToggle;
    void renderMixerButton(NUIRenderer& renderer, const NUIRect& bounds);
};
```

---

### 2. Basic Recording Capability (Priority: MEDIUM)

#### Implementation Plan

**Step 1: Recording Infrastructure**
```cpp
// Recorder.h
class Recorder {
public:
    struct RecordingConfig {
        int inputDeviceId = -1; // -1 for default
        int sampleRate = 48000;
        int bufferSize = 256;
        int numChannels = 2;
        bool enableMonitoring = true;
    };
    
    bool startRecording(const RecordingConfig& config);
    void stopRecording();
    bool isRecording() const { return m_isRecording; }
    
    // Recording data access
    std::shared_ptr<AudioBuffer> getRecordedBuffer() const { return m_recordedBuffer; }
    double getRecordingDuration() const;
    
    // Callbacks
    void setOnRecordingData(std::function<void(const float*, uint32_t)> callback) {
        m_onRecordingData = callback;
    }
    
private:
    void recordingCallback(const float* input, uint32_t numFrames);
    
    static int staticRecordingCallback(void* outputBuffer, const void* inputBuffer,
                                      uint32_t numFrames, double streamTime, void* userData);
    
    RecordingConfig m_config;
    bool m_isRecording = false;
    std::shared_ptr<AudioBuffer> m_recordedBuffer;
    std::function<void(const float*, uint32_t)> m_onRecordingData;
    std::unique_ptr<AudioDeviceManager> m_audioManager;
};

// AudioInputMonitor.h
class AudioInputMonitor {
public:
    void setInputDevice(int deviceId);
    void setMonitoringEnabled(bool enabled);
    bool isMonitoring() const { return m_monitoringEnabled; }
    
    void startMonitoring();
    void stopMonitoring();
    
    float getInputLevel() const { return m_inputLevel; }
    
private:
    void updateInputLevel(const float* input, uint32_t numFrames);
    
    bool m_monitoringEnabled = false;
    int m_inputDeviceId = -1;
    float m_inputLevel = 0.0f;
    std::atomic<float> m_currentLevel{0.0f};
    std::unique_ptr<AudioDeviceManager> m_audioManager;
};
```

**Step 2: Track Recording Integration**
```cpp
// Track.h (enhancement)
class Track {
public:
    void startRecording();
    void stopRecording();
    bool isRecording() const { return m_isRecording; }
    
    // Recording settings
    void setRecordEnabled(bool enabled) { m_recordEnabled = enabled; }
    bool isRecordEnabled() const { return m_recordEnabled; }
    
    // Input monitoring
    void setInputMonitoring(bool enabled) { m_inputMonitoring = enabled; }
    bool isInputMonitoring() const { return m_inputMonitoring; }
    
private:
    bool m_isRecording = false;
    bool m_recordEnabled = false;
    bool m_inputMonitoring = false;
    std::shared_ptr<Recorder> m_recorder;
    std::shared_ptr<AudioInputMonitor> m_inputMonitor;
    
    void handleRecordedData(const float* data, uint32_t frames);
};

// TrackManager.h (enhancement)
class TrackManager {
public:
    void startRecordingOnTrack(std::shared_ptr<Track> track);
    void stopRecordingOnTrack(std::shared_ptr<Track> track);
    void stopAllRecording();
    
    // Record state management
    std::vector<std::shared_ptr<Track>> getRecordingTracks() const;
    bool hasRecordingTracks() const;
    
private:
    std::mutex m_recordingMutex;
    std::unordered_set<std::shared_ptr<Track>> m_recordingTracks;
};
```

**Step 3: Recording UI**
```cpp
// RecordingIndicator.h
class RecordingIndicator : public NUIComponent {
public:
    void setRecording(bool recording);
    bool isRecording() const { return m_recording; }
    
    void render(NUIRenderer& renderer) override;
    
private:
    bool m_recording = false;
    float m_pulsePhase = 0.0f;
};

// RecordButton.h
class RecordButton : public NUIComponent {
public:
    void setRecordEnabled(bool enabled);
    void setRecording(bool recording);
    void setArmedTracks(int count);
    
    void render(NUIRenderer& renderer) override;
    void onMouseClick(const MouseClickEvent& event) override;
    
private:
    void renderRecordButton(NUIRenderer& renderer, const NUIRect& bounds);
    
    bool m_recordEnabled = false;
    bool m_recording = false;
    int m_armedTracks = 0;
    
    std::function<void()> m_onRecordClicked;
};

// TrackHeader.h (enhancement)
class TrackHeader {
public:
    void setRecordEnabled(bool enabled);
    void setRecording(bool recording);
    void setInputMonitoring(bool monitoring);
    
    void render(NUIRenderer& renderer) override;
    void onMouseClick(const MouseClickEvent& event) override;
    
private:
    void renderRecordArmButton(NUIRenderer& renderer, const NUIRect& bounds);
    void renderInputMonitorButton(NUIRenderer& renderer, const NUIRect& bounds);
    
    bool m_recordEnabled = false;
    bool m_recording = false;
    bool m_inputMonitoring = false;
};
```

---

## Development & Testing Improvements

### 1. Comprehensive Unit Testing Framework

#### Implementation Plan

**Step 1: Test Infrastructure**
```cpp
// TestFramework.h
#define NOMAD_TEST_CASE(name) \
    static void name(); \
    struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            TestRunner::registerTest(#name, name); \
        } \
    }; \
    static TestRegistrar_##name registrar_##name; \
    static void name()

#define NOMAD_TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            throw TestFailure("Assertion failed: " #condition, __FILE__, __LINE__); \
        } \
    } while(0)

#define NOMAD_TEST_ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            std::stringstream ss; \
            ss << "Assertion failed: " << #expected << " == " << #actual \
               << "\nExpected: " << (expected) << "\nActual: " << (actual); \
            throw TestFailure(ss.str(), __FILE__, __LINE__); \
        } \
    } while(0)

// TestRunner.h
class TestRunner {
public:
    static void registerTest(const std::string& name, std::function<void()> test);
    static int runAllTests();
    static void setTestFilter(const std::string& filter);
    
private:
    static std::vector<std::pair<std::string, std::function<void()>>> m_tests;
    static std::string m_filter;
};

struct TestFailure : public std::exception {
    TestFailure(const std::string& message, const char* file, int line)
        : m_message(message + " at " + file + ":" + std::to_string(line)) {}
    
    const char* what() const noexcept override {
        return m_message.c_str();
    }
    
private:
    std::string m_message;
};
```

**Step 2: Audio Engine Tests**
```cpp
// AudioEngineTests.cpp
NOMAD_TEST_CASE(TestTrackManagerBasicOperations) {
    auto trackManager = std::make_unique<TrackManager>();
    
    // Test track creation
    auto track1 = trackManager->addTrack("Test Track 1");
    NOMAD_TEST_ASSERT(track1 != nullptr);
    NOMAD_TEST_ASSERT_EQ(1, trackManager->getTrackCount());
    
    auto track2 = trackManager->addTrack("Test Track 2");
    NOMAD_TEST_ASSERT(track2 != nullptr);
    NOMAD_TEST_ASSERT_EQ(2, trackManager->getTrackCount());
    
    // Test track retrieval
    auto retrievedTrack = trackManager->getTrack(0);
    NOMAD_TEST_ASSERT(retrievedTrack != nullptr);
    NOMAD_TEST_ASSERT_EQ("Test Track 1", retrievedTrack->getName());
    
    // Test track removal
    trackManager->removeTrack(0);
    NOMAD_TEST_ASSERT_EQ(1, trackManager->getTrackCount());
}

NOMAD_TEST_CASE(TestTrackAudioProcessing) {
    auto track = std::make_shared<Track>("Test Track", 0);
    
    // Load test audio data
    const int numFrames = 512;
    std::vector<float> testData(numFrames, 1.0f);
    track->loadAudioData(testData.data(), numFrames, 48000.0);
    
    // Process audio
    std::vector<float> outputBuffer(numFrames * 2, 0.0f);
    track->play();
    track->processAudio(outputBuffer.data(), numFrames, 0.0, 48000.0);
    
    // Verify output
    for (int i = 0; i < numFrames * 2; ++i) {
        NOMAD_TEST_ASSERT(outputBuffer[i] > 0.0f); // Should have some audio
    }
}

NOMAD_TEST_CASE(TestTrackVolumeAndPan) {
    auto track = std::make_shared<Track>("Test Track", 0);
    
    // Test volume control
    track->setVolume(0.5f);
    NOMAD_TEST_ASSERT_EQ(0.5f, track->getVolume());
    
    // Test pan control
    track->setPan(-0.5f);
    NOMAD_TEST_ASSERT_EQ(-0.5f, track->getPan());
    
    track->setPan(0.5f);
    NOMAD_TEST_ASSERT_EQ(0.5f, track->getPan());
    
    // Test mute
    track->setMuted(true);
    NOMAD_TEST_ASSERT(track->isMuted());
    
    track->setMuted(false);
    NOMAD_TEST_ASSERT(!track->isMuted());
    
    // Test solo
    track->setSolo(true);
    NOMAD_TEST_ASSERT(track->isSoloed());
    
    track->setSolo(false);
    NOMAD_TEST_ASSERT(!track->isSoloed());
}
```

**Step 3: Configuration Management Tests**
```cpp
// ConfigTests.cpp
NOMAD_TEST_CASE(TestConfigValueOperations) {
    ConfigValue<int> configValue(100);
    
    // Test initial value
    NOMAD_TEST_ASSERT_EQ(100, configValue.get());
    
    // Test value changes
    configValue.set(200);
    NOMAD_TEST_ASSERT_EQ(200, configValue.get());
    
    // Test reset
    configValue.reset();
    NOMAD_TEST_ASSERT_EQ(100, configValue.get());
}

NOMAD_TEST_CASE(TestConfigSerialization) {
    auto& config = AppConfig::getInstance();
    
    // Set some test values
    config.sampleRate().set(44100);
    config.windowWidth().set(1920);
    config.themeName() = "test-theme";
    
    // Save to temporary file
    std::string tempPath = "test_config.json";
    ConfigManager::getInstance().saveToFile(tempPath);
    
    // Reset values
    config.sampleRate().reset();
    config.windowWidth().reset();
    config.themeName() = "";
    
    // Load from file
    ConfigManager::getInstance().loadFromFile(tempPath);
    
    // Verify values
    NOMAD_TEST_ASSERT_EQ(44100, config.sampleRate());
    NOMAD_TEST_ASSERT_EQ(1920, config.windowWidth());
    NOMAD_TEST_ASSERT_EQ("test-theme", config.themeName());
    
    // Cleanup
    std::filesystem::remove(tempPath);
}
```

**Step 4: Command Pattern Tests**
```cpp
// CommandPatternTests.cpp
class MockCommand : public Command {
public:
    MockCommand(bool& executed, bool& undone) 
        : m_executed(executed), m_undone(undone) {}
    
    bool execute() override {
        m_executed = true;
        return true;
    }
    
    bool undo() override {
        m_undone = true;
        return true;
    }
    
    std::string getDescription() const override { return "Mock Command"; }
    
private:
    bool& m_executed;
    bool& m_undone;
};

NOMAD_TEST_CASE(TestCommandManager) {
    bool executed = false;
    bool undone = false;
    
    auto command = std::make_unique<MockCommand>(executed, undone);
    
    auto& manager = CommandManager::getInstance();
    
    // Test command execution
    manager.executeCommand(std::move(command));
    NOMAD_TEST_ASSERT(executed);
    NOMAD_TEST_ASSERT(manager.canUndo());
    
    // Test undo
    manager.undo();
    NOMAD_TEST_ASSERT(undone);
    NOMAD_TEST_ASSERT(manager.canRedo());
    
    // Test redo
    manager.redo();
    NOMAD_TEST_ASSERT(manager.canUndo());
    NOMAD_TEST_ASSERT(!manager.canRedo());
}

NOMAD_TEST_CASE(TestAddTrackCommand) {
    auto trackManager = std::make_unique<TrackManager>();
    auto command = std::make_unique<AddTrackCommand>(trackManager.get(), "Test Track");
    
    size_t initialCount = trackManager->getTrackCount();
    
    // Execute command
    bool success = command->execute();
    NOMAD_TEST_ASSERT(success);
    NOMAD_TEST_ASSERT_EQ(initialCount + 1, trackManager->getTrackCount());
    
    // Undo command
    success = command->undo();
    NOMAD_TEST_ASSERT(success);
    NOMAD_TEST_ASSERT_EQ(initialCount, trackManager->getTrackCount());
}
```

---

### 2. Performance Profiling Integration

#### Implementation Plan

**Step 1: Tracy Profiler Integration**
```cpp
// ProfilerIntegration.h
#ifdef TRACY_ENABLE
    #define NOMAD_ZONE(name) ZoneScopedN(name)
    #define NOMAD_ZONE_TEXT(text) ZoneText(text, strlen(text))
    #define NOMAD_ZONE_COLOR(color) ZoneColor(color)
#else
    #define NOMAD_ZONE(name) ((void)0)
    #define NOMAD_ZONE_TEXT(text) ((void)0)
    #define NOMAD_ZONE_COLOR(color) ((void)0)
#endif

// PerformanceProfiler.h
class PerformanceProfiler {
public:
    static PerformanceProfiler& getInstance();
    
    // Audio performance tracking
    void beginAudioBlock();
    void endAudioBlock();
    void recordAudioProcessingTime(double timeMs);
    
    // UI performance tracking
    void beginUIFrame();
    void endUIFrame();
    void recordUIFrameTime(double timeMs);
    
    // Memory tracking
    void recordMemoryUsage(size_t bytes);
    void recordAllocation(size_t bytes);
    void recordDeallocation(size_t bytes);
    
    // Reporting
    void generateReport(const std::string& outputPath);
    void reset();
    
    // Metrics access
    struct Metrics {
        double avgAudioProcessingTime = 0.0;
        double maxAudioProcessingTime = 0.0;
        double avgUIFrameTime = 0.0;
        double maxUIFrameTime = 0.0;
        size_t peakMemoryUsage = 0;
        size_t currentMemoryUsage = 0;
    };
    
    Metrics getMetrics() const;
    
private:
    struct AudioMetrics {
        std::deque<double> processingTimes;
        std::atomic<size_t> totalBlocks{0};
        std::atomic<size_t> overdueBlocks{0};
    };
    
    struct UIMetrics {
        std::deque<double> frameTimes;
        std::atomic<size_t> totalFrames{0};
    };
    
    struct MemoryMetrics {
        std::atomic<size_t> currentUsage{0};
        std::atomic<size_t> peakUsage{0};
        std::atomic<size_t> totalAllocations{0};
        std::atomic<size_t> totalDeallocations{0};
    };
    
    AudioMetrics m_audio;
    UIMetrics m_ui;
    MemoryMetrics m_memory;
    
    std::chrono::steady_clock::time_point m_lastAudioBlockStart;
    std::chrono::steady_clock::time_point m_lastUIFrameStart;
};

// Enhanced TrackManager with profiling
void TrackManager::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime) {
    auto& profiler = PerformanceProfiler::getInstance();
    profiler.beginAudioBlock();
    
    NOMAD_ZONE("Audio_Processing");
    
    // ... existing processing code ...
    
    profiler.endAudioBlock();
}

// Enhanced main loop with profiling
void NomadApp::run() {
    while (m_running && m_window->processEvents()) {
        auto& profiler = PerformanceProfiler::getInstance();
        profiler.beginUIFrame();
        
        // ... existing loop code ...
        
        profiler.endUIFrame();
    }
}
```

**Step 2: Memory Profiling**
```cpp
// MemoryProfiler.h
class MemoryProfiler {
public:
    static MemoryProfiler& getInstance();
    
    void recordAllocation(void* ptr, size_t size, const char* file = __FILE__, int line = __LINE__);
    void recordDeallocation(void* ptr, const char* file = __FILE__, int line = __LINE__);
    
    void generateLeakReport(const std::string& outputPath);
    void reset();
    
    struct AllocationInfo {
        void* ptr = nullptr;
        size_t size = 0;
        std::string file;
        int line = 0;
        std::chrono::system_clock::time_point timestamp;
    };
    
    std::vector<AllocationInfo> getActiveAllocations() const;
    size_t getCurrentMemoryUsage() const;
    size_t getPeakMemoryUsage() const;
    
private:
    struct Allocation {
        size_t size;
        std::string file;
        int line;
        std::chrono::system_clock::time_point timestamp;
    };
    
    std::unordered_map<void*, Allocation> m_allocations;
    std::mutex m_mutex;
    std::atomic<size_t> m_currentUsage{0};
    std::atomic<size_t> m_peakUsage{0};
};

// Global new/delete operators with profiling
void* operator new(size_t size) {
    void* ptr = malloc(size);
    if (ptr) {
        MemoryProfiler::getInstance().recordAllocation(ptr, size);
    }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    if (ptr) {
        MemoryProfiler::getInstance().recordDeallocation(ptr);
        free(ptr);
    }
}

void operator delete(void* ptr, size_t size) noexcept {
    if (ptr) {
        MemoryProfiler::getInstance().recordDeallocation(ptr);
        free(ptr);
    }
}
```

---

## Implementation Timeline

### Phase 1: Foundation (Weeks 1-4)
**Week 1-2: Code Organization**
- Refactor main.cpp into modular components
- Implement configuration management system
- Set up comprehensive testing framework

**Week 3-4: Core Architecture**
- Implement command pattern for undo/redo
- Enhance event system
- Create plugin architecture foundation

### Phase 2: Essential Features (Weeks 5-8)
**Week 5-6: Sample Manipulation**
- Implement drag-and-drop functionality
- Add sample positioning and management
- Create visual feedback system

**Week 7-8: Project Management**
- Implement save/load system with JSON
- Add auto-save functionality
- Create project templates

### Phase 3: User Experience (Weeks 9-12)
**Week 9-10: Mixing Controls**
- Add volume/pan controls per track
- Implement mute/solo functionality
- Create mixer interface

**Week 11-12: Audio Recording**
- Implement basic recording capability
- Add input monitoring
- Create recording UI

### Phase 4: Performance & Polish (Weeks 13-16)
**Week 13-14: Performance Optimization**
- Optimize audio thread pool
- Enhance UI rendering pipeline
- Implement memory management improvements

**Week 15-16: Quality Assurance**
- Comprehensive testing and debugging
- Performance profiling and optimization
- Documentation and user guides

### Phase 5: Advanced Features (Weeks 17-20)
**Week 17-18: MIDI Support**
- Implement MIDI I/O
- Create piano roll interface
- Add MIDI file import/export

**Week 19-20: Cross-Platform**
- Begin Linux audio backend (ALSA)
- Start macOS support (CoreAudio)
- Abstract platform-specific code

---

## Success Metrics & Validation

### Performance Targets
| Metric | Current | Target | Measurement Method |
|--------|---------|--------|-------------------|
| Audio Latency | ~8ms | <5ms | Hardware measurement with focusrite |
| UI Frame Rate | 45-60fps | Consistent 60fps | Frame time monitoring |
| Memory Usage | ~800MB | <500MB | Heap profiler with typical project |
| CPU Usage | 30-40% | <20% | Performance profiler with 8-track playback |
| Load Time | ~3s | <2s | Startup time measurement |

### Code Quality Targets
| Metric | Current | Target | Tools |
|--------|---------|--------|-------|
| Test Coverage | ~20% | >80% | GCOVR, custom coverage tools |
| Cyclomatic Complexity | 15-25 | <10 | Cppcheck, custom analyzer |
| Memory Leaks | Detected | Zero | AddressSanitizer, Valgrind |
| API Documentation | 60% | >90% | Doxygen coverage analysis |
| Static Analysis Issues | Many | <5 | clang-tidy, cppcheck |

### Feature Completion Matrix
| Feature | Current Status | Target Status | Dependencies |
|---------|---------------|---------------|--------------|
| Sample Drag-Drop | Missing | Complete | Timeline, File browser |
| Project Save/Load | Missing | Complete | Command pattern |
| Undo/Redo | Missing | Complete | Command pattern |
| Volume/Pan Controls | Basic | Complete | UI framework |
| MIDI Support | Missing | Basic | Audio engine |
| VST3 Hosting | Missing | Foundation | Plugin architecture |
| Recording | Missing | Basic | Audio engine |
| Linux Support | Planned | Basic | Platform abstraction |

### Validation Process
1. **Unit Testing**: All critical components must have >80% coverage
2. **Integration Testing**: End-to-end workflows tested
3. **Performance Testing**: Automated benchmarks with regression detection
4. **Memory Testing**: AddressSanitizer builds must pass
5. **User Testing**: Beta users validate workflow improvements

---

## Risk Assessment & Mitigation

### High-Risk Areas
1. **Audio Thread Optimization** - Risk: Real-time violations, audio dropouts
   - Mitigation: Extensive testing with audio measurement tools, fallback to single-threaded mode
   - Timeline: Week 13-14

2. **Plugin Architecture** - Risk: Interface compatibility, security issues
   - Mitigation: Sandboxing, extensive compatibility testing
   - Timeline: Week 1-4 (foundation), Week 17-20 (implementation)

3. **Configuration Management** - Risk: Configuration corruption, user data loss
   - Mitigation: Versioned configuration, backup/recovery, validation
   - Timeline: Week 1-2

### Medium-Risk Areas
1. **Sample Drag-Drop** - Risk: UI performance impact, file format issues
   - Mitigation: Progressive loading, format validation
   - Timeline: Week 5-6

2. **Project Save/Load** - Risk: Data corruption, version incompatibility
   - Mitigation: Robust serialization, version migration, validation
   - Timeline: Week 7-8

### Low-Risk Areas
1. **UI Rendering Optimization** - Risk: Visual artifacts, performance regression
   - Mitigation: Comprehensive visual testing, fallback rendering
   - Timeline: Week 13-14

2. **Memory Management** - Risk: Fragmentation, allocation failures
   - Mitigation: Object pools, smart pointers, memory profiling
   - Timeline: Week 13-14

### Success Criteria
- **All Phase 1 tasks completed** before moving to Phase 2
- **Performance targets met** in controlled testing environment
- **No regression** in existing functionality
- **User acceptance** of core workflow improvements
- **Code quality metrics** above target thresholds

### Rollback Plan
- Each phase includes rollback procedures
- Feature flags for new functionality
- Configuration backup and recovery
- Version control with clear tagging

---

**Next Steps:**
1. Review and approve this implementation plan
2. Begin Phase 1: Code organization and foundation
3. Establish development workflow and CI/CD pipeline
4. Set up performance monitoring and testing infrastructure

*This document serves as the technical blueprint for NOMAD DAW core improvements. Each phase builds upon previous achievements while maintaining stability and user experience.*
                AppConfig::getInstance().sampleRate().set(audio["sampleRate"]);
            }
            if (audio.contains("bufferSize")) {
                AppConfig::getInstance().bufferSize().set(audio["bufferSize"]);
            }
            if (audio.contains("masterVolume")) {
                AppConfig::getInstance().masterVolume().set(audio["masterVolume"]);
            }
        }
        
        // Apply UI settings
        if (config.contains("ui")) {
            auto& ui = config["ui"];
            if (ui.contains("windowWidth")) {
                AppConfig::getInstance().windowWidth().set(ui["windowWidth"]);
            }
            if (ui.contains("windowHeight")) {
                AppConfig::getInstance().windowHeight().set(ui["windowHeight"]);
            }
            if (ui.contains("fullscreen")) {
                AppConfig::getInstance().fullscreen().set(ui["fullscreen"]);
            }
            if (ui.contains("theme")) {
                AppConfig::getInstance().themeName() = ui["theme"];
            }
        }
        
        // Apply performance settings
        if (config.contains("performance")) {
            auto& perf = config["performance"];
            if (perf.contains("audioThreads")) {
                AppConfig::getInstance().audioThreads().set(perf["audioThreads"]);
            }
            if (perf.contains("vsyncEnabled")) {
                AppConfig::getInstance().vsyncEnabled().set(perf["vsyncEnabled"]);
            }
        }
        
        m_configFilePath = configPath;
        Log::info("Configuration loaded from: " + configPath);
        return true;
        
    } catch (const std::exception& e) {
        Log::error("Failed to load configuration: " + std::string(e.what()));
        return false;
    }
}
```

**Step 3: Configuration File Format**
```json
{
    "audio": {
        "sampleRate": 48000,
        "bufferSize": 256,
        "masterVolume": 1.0,
        "lowLatencyMode": true,
        "exclusiveMode": true
    },
    "ui": {
        "windowWidth": 1280,
        "windowHeight": 720,
        "fullscreen": false,
        "theme": "nomad-dark",
        "showFPS": true,
        "adaptiveFPS": true
    },
    "performance": {
        "audioThreads": 4,
        "vsyncEnabled": true,
        "maxCPUUsage": 80.0,
        "renderQuality": "high"
    },
    "paths": {
        "lastProjectPath": "C:/Users/Music/Projects",
        "samplesDirectory": "C:/Users/Music/Samples",
        "pluginsDirectory": "C:/Program Files/VST3"
    },
    "keyboard": {
        "shortcuts": {
            "play": "space",
            "stop": "escape",
            "record": "r",
            "save": "ctrl+s",
            "open": "ctrl+o"
        }
    }
}
```

**Step 4: Integration with Existing Code**
```cpp
// Before: Hard-coded values
void AudioDeviceManager::initialize() {
    m_sampleRate = 48000; // Hard-coded
    m_bufferSize = 256;   // Hard-coded
}

// After: Configuration-based
void AudioDeviceManager::initialize() {
    auto& config = AppConfig::getInstance();
    m_sampleRate = config.sampleRate();
    m_bufferSize = config.bufferSize();
    
    Log::info("Audio initialized: " + std::to_string(m_sampleRate) + "Hz, " + 
              std::to_string(m_bufferSize) + " samples");
}
```
}
```

**Step 5: UI Integration**
```cpp
// UndoRedoButtons.h
class UndoRedoButtons : public NUIComponent {
public:
    void render(NUIRenderer& renderer) override;
    void onMouseClick(const MouseClickEvent& event) override;
    
private:
    void renderUndoButton(NUIRenderer& renderer, const NUIRect& bounds);
    void renderRedoButton(NUIRenderer& renderer, const NUIRect& bounds);
    
    bool m_hoveringUndo = false;
    bool m_hoveringRedo = false;
};

// TransportBar.h (enhancement)
class TransportBar {
public:
    void setUndoButton(std::unique_ptr<UndoRedoButtons> undoRedo);
    
private:
    std::unique_ptr<UndoRedoButtons> m_undoRedoButtons;
};

// Keyboard Shortcuts
void EventHandler::setupKeyboardShortcuts() {
    m_window->setKeyCallback([this](int key, bool pressed) {
        if (pressed) {
            auto& commandManager = CommandManager::getInstance();
            
            if (key == NOMAD_KEY_CTRL_Z) {
                if (key == NOMAD_KEY_SHIFT) {
                    commandManager.redo(); // Ctrl+Shift+Z or Ctrl+Y
                } else {
                    commandManager.undo(); // Ctrl+Z
                }
            }
            // ... other shortcuts
        }
    });
}
```

#### History Management
```cpp
// CommandHistory.h
class CommandHistory {
public:
    struct HistoryEntry {
        std::string description;
        std::chrono::system_clock::time_point timestamp;
        size_t undoStackSize;
        size_t redoStackSize;
    };
    
    void recordCommand(const std::string& description);
    std::vector<HistoryEntry> getHistory(size_t maxEntries = 20) const;
    void clearHistory();
    
private:
    std::vector<HistoryEntry> m_history;
    mutable std::mutex m_historyMutex;
};
```