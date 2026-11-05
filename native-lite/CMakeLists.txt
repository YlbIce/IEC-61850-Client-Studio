cmake_minimum_required(VERSION 3.16)
project(Iec61850ClientLite VERSION 0.1.6 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601 NOMINMAX)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
foreach(feature INCLUDE_GOOSE_SUPPORT IEC61850_L2_GOOSE IEC61850_L2_SMV IEC61850_R_GOOSE IEC61850_R_SMV IEC61850_SETTING_GROUPS IEC61850_LOG_SERVICE IEC61850_SNTP_CLIENT)
  set(CONFIG_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(CONFIG_IEC61850_SERVICE_TRACKING ON CACHE BOOL "Upstream server headers require its type declaration" FORCE)
add_subdirectory(../third_party/libiec61850 iec61850 EXCLUDE_FROM_ALL)
# Upstream auto-detects a bundled WinPcap SDK even with GOOSE/SV disabled.
# This MMS-only binary must not link the SDK or require an installed driver.
foreach(lib iec61850 hal)
  foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(deps ${lib} ${property})
    if(deps)
      list(FILTER deps EXCLUDE REGEX "[Ww][Ii][Nn][Pp][Cc][Aa][Pp]")
      set_property(TARGET ${lib} PROPERTY ${property} "${deps}")
    endif()
  endforeach()
endforeach()
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Test)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(IEC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libiec61850)
# Protocol code depends on Qt Core only and has no widget dependency.
add_library(lite_protocol STATIC
    src/session.cpp src/session.h
    src/connection_options.cpp src/connection_options.h
    src/model_reader.cpp src/mms_value.cpp
    src/report_pipeline.cpp src/report_pipeline.h
    src/report_service.cpp src/control_service.cpp src/monitor.cpp
)
target_include_directories(lite_protocol PUBLIC
    src
    ${IEC_ROOT}/src/iec61850/inc
    ${IEC_ROOT}/src/common/inc
    ${IEC_ROOT}/src/mms/inc
    ${IEC_ROOT}/hal/inc
    ${IEC_ROOT}/src/goose
    ${IEC_ROOT}/src/sampled_values
    ${IEC_ROOT}/src/r_session
    ${IEC_ROOT}/src/logging
    ${CMAKE_CURRENT_BINARY_DIR}/iec61850/config
)
target_link_libraries(lite_protocol PUBLIC Qt5::Core iec61850)

# Application and integration tests link the same compiled presentation layer.
add_library(lite_core STATIC
    src/history_model.cpp src/history_model.h
    src/table_export.cpp src/table_export.h
    src/history_panel.cpp src/history_panel.h
    src/monitor_panel.cpp src/monitor_panel.h
    src/striped_log_view.cpp src/striped_log_view.h
    src/event_log.cpp src/event_log.h
    src/point_model.cpp src/point_model.h
    src/studio_style.cpp src/studio_style.h src/studio_assets.qrc
    src/main_window.cpp src/main_window.h
    src/connection_dialog.cpp src/connection_dialog.h
    src/report_dialog.cpp src/report_dialog.h
    src/control_dialog.cpp src/control_dialog.h
    src/test_result_dialog.cpp src/test_result_dialog.h
)
target_link_libraries(lite_core PUBLIC Qt5::Widgets lite_protocol)
add_executable(Iec61850ClientLite WIN32 src/main.cpp)
if(WIN32)
    enable_language(RC)
    configure_file(src/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(Iec61850ClientLite PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()
target_link_libraries(Iec61850ClientLite PRIVATE lite_core)
add_executable(lite_tests tests/integration.cpp)
target_link_libraries(lite_tests PRIVATE lite_core Qt5::Test)
if(MINGW)
  target_link_options(Iec61850ClientLite PRIVATE -Wl,--major-subsystem-version,6,--minor-subsystem-version,1)
endif()
