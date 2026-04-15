# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-src")
  file(MAKE_DIRECTORY "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-src")
endif()
file(MAKE_DIRECTORY
  "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-build"
  "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix"
  "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/tmp"
  "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp"
  "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src"
  "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/venera/midnight/night_fund/build_verify/_deps/jsoncpp-subbuild/jsoncpp-populate-prefix/src/jsoncpp-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
