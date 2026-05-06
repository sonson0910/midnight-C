# CMake generated Testfile for 
# Source directory: D:/venera/midnight/night_fund/tests
# Build directory: D:/venera/midnight/night_fund/.cmake-build/manual/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[core_tests]=] "D:/venera/midnight/night_fund/.cmake-build/manual/bin/midnight-tests.exe")
set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/venera/midnight/night_fund/tests/CMakeLists.txt;49;add_test;D:/venera/midnight/night_fund/tests/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
