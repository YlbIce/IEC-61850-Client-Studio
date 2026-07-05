# CMake generated Testfile for 
# Source directory: D:/WORKSPACE/Electron/Iec61850ClientStudio
# Build directory: D:/WORKSPACE/Electron/Iec61850ClientStudio/backend/build-tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[studio-tests]=] "D:/WORKSPACE/Electron/Iec61850ClientStudio/backend/bin/studio-tests.exe")
set_tests_properties([=[studio-tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/WORKSPACE/Electron/Iec61850ClientStudio/CMakeLists.txt;114;add_test;D:/WORKSPACE/Electron/Iec61850ClientStudio/CMakeLists.txt;0;")
subdirs("third_party/libiec61850")
