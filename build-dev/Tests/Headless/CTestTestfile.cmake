# CMake generated Testfile for 
# Source directory: /home/currentsuspect/.openclaw/workspace/Aestra/Tests/Headless
# Build directory: /home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/Headless
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[HeadlessOfflineRenderer]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/Headless/HeadlessOfflineRenderer")
set_tests_properties([=[HeadlessOfflineRenderer]=] PROPERTIES  ENVIRONMENT "AESTRA_HEADLESS=1" LABELS "headless;offline;rendering" _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/Headless/CMakeLists.txt;17;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/Headless/CMakeLists.txt;0;")
add_test([=[OfflineRenderRegressionTest]=] "/home/currentsuspect/.openclaw/workspace/Aestra/build-dev/Tests/Headless/OfflineRenderRegressionTest")
set_tests_properties([=[OfflineRenderRegressionTest]=] PROPERTIES  ENVIRONMENT "AESTRA_HEADLESS=1" LABELS "headless;regression;offline" _BACKTRACE_TRIPLES "/home/currentsuspect/.openclaw/workspace/Aestra/Tests/Headless/CMakeLists.txt;34;add_test;/home/currentsuspect/.openclaw/workspace/Aestra/Tests/Headless/CMakeLists.txt;0;")
