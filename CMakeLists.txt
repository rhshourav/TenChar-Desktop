cmake_minimum_required(VERSION 3.21)
project(TenCharDesktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TC_SANITIZE "Build core tests with address and undefined-behaviour sanitisers" OFF)
option(TC_BUILD_APP "Build the Qt 6 application" ON)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

# Warnings as errors: on for GCC/Clang. Off by default for MSVC, which has not been tried on this code yet.
if(MSVC)
  set(_tc_werror_default OFF)
else()
  set(_tc_werror_default ON)
endif()
option(TC_WERROR "Treat compiler warnings as errors" ${_tc_werror_default})

if(MSVC)
  add_compile_options(/W4 /guard:cf /GS /utf-8 /permissive-)
  add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
  if(TC_WERROR)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -fstack-protector-strong)
  if(TC_WERROR)
    add_compile_options(-Werror)
  endif()
  if(NOT APPLE AND NOT WIN32)
    add_compile_options(-D_FORTIFY_SOURCE=2)
    add_link_options(-Wl,-z,relro,-z,now)
  endif()
endif()
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()
if(TC_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# core: no Qt types, no UI code. Builds and tests on its own.
add_library(tc_core STATIC
  core/src/bytes.cpp core/src/random.cpp core/src/kdf.cpp core/src/aead.cpp core/src/x25519.cpp
  core/src/json.cpp core/src/limits.cpp core/src/wire.cpp core/src/pairing.cpp
  core/src/handshake.cpp core/src/ratchet.cpp)
target_include_directories(tc_core PUBLIC core/include)
target_link_libraries(tc_core PUBLIC OpenSSL::Crypto Threads::Threads)

# vault: state codec and passphrase key handling. Touches secrets; no Qt.
add_library(tc_vault STATIC vault/src/state.cpp vault/src/vault.cpp)
target_include_directories(tc_vault PUBLIC vault/include)
target_link_libraries(tc_vault PUBLIC tc_core)

# relayclient: HTTP client for one relay and the multi-relay pairing flow. No Qt: the transport is an interface.
add_library(tc_relayclient STATIC relayclient/src/relay.cpp relayclient/src/pairing_flow.cpp relayclient/src/relay_address.cpp)
target_include_directories(tc_relayclient PUBLIC relayclient/include)
target_link_libraries(tc_relayclient PUBLIC tc_core)

# session: the application logic (a port of AppViewModel.kt). No Qt, so it is tested without a display.
add_library(tc_session STATIC session/src/session.cpp)
target_include_directories(tc_session PUBLIC session/include)
target_link_libraries(tc_session PUBLIC tc_vault tc_relayclient)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE tc_core)
target_compile_definitions(core_tests PRIVATE TC_VECTORS_DIR="${CMAKE_SOURCE_DIR}/vectors")
add_test(NAME core_tests COMMAND core_tests)

add_executable(vault_tests tests/vault_tests.cpp)
target_link_libraries(vault_tests PRIVATE tc_vault)
add_test(NAME vault_tests COMMAND vault_tests)

add_executable(relay_tests tests/relay_tests.cpp)
target_link_libraries(relay_tests PRIVATE tc_relayclient)
add_test(NAME relay_tests COMMAND relay_tests)

add_executable(session_tests tests/session_tests.cpp)
target_link_libraries(session_tests PRIVATE tc_session)
add_test(NAME session_tests COMMAND session_tests)

add_executable(fuzz_tests tests/fuzz_tests.cpp)
target_link_libraries(fuzz_tests PRIVATE tc_session)
add_test(NAME fuzz_tests COMMAND fuzz_tests 8000)

add_executable(strings_tests tests/strings_tests.cpp)
target_link_libraries(strings_tests PRIVATE tc_core)
target_compile_definitions(strings_tests PRIVATE TC_APP_DIR="${CMAKE_SOURCE_DIR}/app")
add_test(NAME strings_tests COMMAND strings_tests)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME contrast COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/contrast.py)
endif()

if(TC_BUILD_APP)
  add_subdirectory(app)
endif()
