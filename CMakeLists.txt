cmake_minimum_required(VERSION 3.24)
project(Iec61850ClientStudio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/libiec61850 EXCLUDE_FROM_ALL)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/backend/generated")

add_executable(iec61850-client-backend
  backend/src/main.cpp
  "${GENERATED_DIR}/iec61850studio.pb.cc"
  "${GENERATED_DIR}/iec61850studio.grpc.pb.cc"
)

target_include_directories(iec61850-client-backend PRIVATE
  "${GENERATED_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/iec61850/inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/common/inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/goose"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/sampled_values"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/r_session"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/mms/inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/mms/inc_private"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/mms/iso_mms/asn1c"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/src/logging"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/hal/inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libiec61850/config"
)

target_compile_definitions(iec61850-client-backend PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)

target_link_libraries(iec61850-client-backend PRIVATE
  gRPC::grpc++
  protobuf::libprotobuf
  iec61850
)

set_target_properties(iec61850-client-backend PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/backend/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/backend/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/backend/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/backend/bin"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/backend/bin"
)

if (MSVC)
  target_compile_options(iec61850-client-backend PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(iec61850-client-backend PRIVATE -Wall -Wextra -pedantic)
endif()
