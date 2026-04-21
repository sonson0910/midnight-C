# CMake generated Testfile for 
# Source directory: /media/son/Projects1/venera/midnight/night_fund/tests
# Build directory: /media/son/Projects1/venera/midnight/night_fund/build_linux_walletmgr/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[core_tests]=] "/media/son/Projects1/venera/midnight/night_fund/build_linux_walletmgr/bin/midnight-tests")
set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/media/son/Projects1/venera/midnight/night_fund/tests/CMakeLists.txt;39;add_test;/media/son/Projects1/venera/midnight/night_fund/tests/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
