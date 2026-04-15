# CMake generated Testfile for 
# Source directory: D:/venera/midnight/night_fund/tests
# Build directory: D:/venera/midnight/night_fund/build_verify/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[core_tests]=] "D:/venera/midnight/night_fund/build_verify/bin/Debug/midnight-tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/venera/midnight/night_fund/tests/CMakeLists.txt;39;add_test;D:/venera/midnight/night_fund/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[core_tests]=] "D:/venera/midnight/night_fund/build_verify/bin/Release/midnight-tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/venera/midnight/night_fund/tests/CMakeLists.txt;39;add_test;D:/venera/midnight/night_fund/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[core_tests]=] "D:/venera/midnight/night_fund/build_verify/bin/MinSizeRel/midnight-tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/venera/midnight/night_fund/tests/CMakeLists.txt;39;add_test;D:/venera/midnight/night_fund/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[core_tests]=] "D:/venera/midnight/night_fund/build_verify/bin/RelWithDebInfo/midnight-tests.exe")
  set_tests_properties([=[core_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/venera/midnight/night_fund/tests/CMakeLists.txt;39;add_test;D:/venera/midnight/night_fund/tests/CMakeLists.txt;0;")
else()
  add_test([=[core_tests]=] NOT_AVAILABLE)
endif()
subdirs("../_deps/googletest-build")
