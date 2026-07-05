# Install script for directory: D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/Iec61850ClientStudio")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Development" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/libiec61850" TYPE FILE FILES
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/hal_base.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/hal_time.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/hal_thread.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/hal_filesystem.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/hal_ethernet.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/hal_socket.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/tls_config.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/hal/inc/tls_ciphers.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/common/inc/libiec61850_common_api.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/common/inc/linked_list.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/common/inc/sntp_client.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/iec61850/inc/iec61850_client.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/iec61850/inc/iec61850_common.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/iec61850/inc/iec61850_server.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/iec61850/inc/iec61850_model.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/iec61850/inc/iec61850_cdc.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/iec61850/inc/iec61850_dynamic_model.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/iec61850/inc/iec61850_config_file_parser.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/mms/inc/mms_value.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/mms/inc/mms_common.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/mms/inc/mms_types.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/mms/inc/mms_type_spec.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/mms/inc/mms_client_connection.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/mms/inc/mms_server.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/mms/inc/iso_connection_parameters.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/goose/goose_subscriber.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/goose/goose_receiver.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/goose/goose_publisher.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/sampled_values/sv_subscriber.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/sampled_values/sv_publisher.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/r_session/r_session.h"
    "D:/WORKSPACE/Electron/Iec61850ClientStudio/third_party/libiec61850/src/logging/logging_api.h"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("D:/WORKSPACE/Electron/Iec61850ClientStudio/backend/build-tests/third_party/libiec61850/hal/cmake_install.cmake")
  include("D:/WORKSPACE/Electron/Iec61850ClientStudio/backend/build-tests/third_party/libiec61850/src/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/WORKSPACE/Electron/Iec61850ClientStudio/backend/build-tests/third_party/libiec61850/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
