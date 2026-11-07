cmake_minimum_required(VERSION 3.16)
project(LibraryManagementSystem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# macOS + Homebrew: libraries live under the brew prefix, which CMake doesn't search by default.
if(APPLE)
  execute_process(COMMAND brew --prefix OUTPUT_VARIABLE BREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(BREW_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${BREW_PREFIX}" "${BREW_PREFIX}/opt/openssl" "${BREW_PREFIX}/opt/curl" "${BREW_PREFIX}/opt/sqlite")
    include_directories("${BREW_PREFIX}/include")
  endif()
endif()

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Everything except the web layer lives in one static library so the
# server, the reminder job and the seeder all share the same classes.
add_library(librarycore STATIC
  src/db.cpp
  src/repositories.cpp
  src/library_service.cpp
  src/email_service.cpp
  src/auth.cpp
  src/reminder_service.cpp)
target_include_directories(librarycore PUBLIC include third_party)
target_link_libraries(librarycore PUBLIC SQLite3::SQLite3 CURL::libcurl OpenSSL::Crypto Threads::Threads)
target_compile_options(librarycore PRIVATE -Wall -Wextra)

add_executable(library_server src/main.cpp)
target_link_libraries(library_server PRIVATE librarycore)

add_executable(reminder_job src/reminder_job.cpp)
target_link_libraries(reminder_job PRIVATE librarycore)

add_executable(seed_data src/seed_data.cpp)
target_link_libraries(seed_data PRIVATE librarycore)

# Tests: `ctest` after building, or run ./library_tests directly.
enable_testing()
add_executable(library_tests tests/test_library.cpp)
target_link_libraries(library_tests PRIVATE librarycore)
add_test(NAME library_tests COMMAND library_tests)
