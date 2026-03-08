# CMake generated Testfile for 
# Source directory: /home/currentsuspect/.openclaw/workspace/Aestra/Tests
# Build directory: /home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[AestraAudioPerformanceTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/AestraAudioPerformanceTest")
set_tests_properties([=[AestraAudioPerformanceTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;133;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[AestraAtomicSaveTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/AestraAtomicSaveTest")
set_tests_properties([=[AestraAtomicSaveTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;158;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[AestraWavLoaderTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/AestraWavLoaderTest")
set_tests_properties([=[AestraWavLoaderTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;193;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[AestraOscillatorTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/AestraOscillatorTest")
set_tests_properties([=[AestraOscillatorTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;226;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[AestraMixerBusTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/AestraMixerBusTest")
set_tests_properties([=[AestraMixerBusTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;231;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[TextRendererSTBSpaceTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/TextRendererSTBSpaceTest")
set_tests_properties([=[TextRendererSTBSpaceTest]=] PROPERTIES  LABELS "ui;regression" _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;267;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[CommandHistoryTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/CommandHistoryTest")
set_tests_properties([=[CommandHistoryTest]=] PROPERTIES  LABELS "commands;phase2" _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;283;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[MoveClipCommandTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/MoveClipCommandTest")
set_tests_properties([=[MoveClipCommandTest]=] PROPERTIES  LABELS "commands;phase2" _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;293;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
add_test([=[MacroCommandTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/MacroCommandTest")
set_tests_properties([=[MacroCommandTest]=] PROPERTIES  LABELS "commands;phase2" _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;303;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/CMakeLists.txt;0;")
subdirs("Headless")
