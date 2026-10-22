cmake_minimum_required(VERSION 3.24)

project(OffWork VERSION 0.4.1 LANGUAGES CXX)

configure_file(src/version.h.in generated/version.h @ONLY)
configure_file(resources/app.manifest.in generated/offwork.manifest @ONLY)
file(WRITE "${CMAKE_BINARY_DIR}/app-version.txt" "${PROJECT_VERSION}\n")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

enable_testing()

function(offwork_target_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
  endif()
  target_compile_definitions(${target} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  target_include_directories(${target} PRIVATE src resources tests "${CMAKE_BINARY_DIR}/generated")
endfunction()

# -- tests -------------------------------------------------------------------

add_executable(OffWorkTests tests/calculator_tests.cpp)
offwork_target_defaults(OffWorkTests)
add_test(NAME calculator COMMAND OffWorkTests)

add_executable(OffWorkMoneyDisplayTests tests/money_display_tests.cpp)
offwork_target_defaults(OffWorkMoneyDisplayTests)
add_test(NAME money_display COMMAND OffWorkMoneyDisplayTests)

add_executable(OffWorkTextFieldTests
  tests/text_field_tests.cpp
  src/text_field.cpp
)
offwork_target_defaults(OffWorkTextFieldTests)
add_test(NAME text_field COMMAND OffWorkTextFieldTests)

add_executable(OffWorkSettingsTests
  tests/settings_tests.cpp
  src/settings.cpp
)
offwork_target_defaults(OffWorkSettingsTests)
target_link_libraries(OffWorkSettingsTests PRIVATE shell32 ole32)
add_test(NAME settings COMMAND OffWorkSettingsTests)

add_executable(OffWorkAutostartTests
  tests/autostart_tests.cpp
  src/autostart.cpp
)
offwork_target_defaults(OffWorkAutostartTests)
target_link_libraries(OffWorkAutostartTests PRIVATE advapi32)
add_test(NAME autostart COMMAND OffWorkAutostartTests)

add_executable(OffWorkReminderTests
  tests/reminder_tests.cpp
  src/reminder.cpp
)
offwork_target_defaults(OffWorkReminderTests)
add_test(NAME reminder COMMAND OffWorkReminderTests)

add_executable(OffWorkOcrTimeTests tests/ocr_time_tests.cpp)
offwork_target_defaults(OffWorkOcrTimeTests)
add_test(NAME ocr_time COMMAND OffWorkOcrTimeTests)

add_executable(OffWorkImageFileTests tests/image_file_tests.cpp)
offwork_target_defaults(OffWorkImageFileTests)
add_test(NAME image_file COMMAND OffWorkImageFileTests)

add_executable(OffWorkUpdateTests tests/update_tests.cpp src/update_info.cpp)
offwork_target_defaults(OffWorkUpdateTests)
add_test(NAME update COMMAND OffWorkUpdateTests)

# -- application -------------------------------------------------------------

if(WIN32)
  add_executable(OffWorkUpdaterTests tests/updater_tests.cpp src/updater.cpp src/update_info.cpp)
  offwork_target_defaults(OffWorkUpdaterTests)
  target_link_libraries(OffWorkUpdaterTests PRIVATE ole32 user32 winhttp)
  add_test(NAME updater COMMAND OffWorkUpdaterTests)
  set_tests_properties(updater PROPERTIES TIMEOUT 15)

  add_executable(OffWorkDropTargetTests tests/drop_target_tests.cpp src/drop_target.cpp)
  offwork_target_defaults(OffWorkDropTargetTests)
  target_link_libraries(OffWorkDropTargetTests PRIVATE ole32 shell32 user32)
  add_test(NAME drop_target COMMAND OffWorkDropTargetTests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/dingtalk_clockin.png")

  add_library(OffWorkOcr STATIC src/ocr.cpp)
  offwork_target_defaults(OffWorkOcr)
  target_link_libraries(OffWorkOcr PUBLIC windowsapp)
  add_executable(OffWorkOcrImageTests tests/ocr_image_tests.cpp)
  offwork_target_defaults(OffWorkOcrImageTests)
  target_link_libraries(OffWorkOcrImageTests PRIVATE OffWorkOcr)
  add_test(NAME ocr_image COMMAND OffWorkOcrImageTests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/dingtalk_clockin.png")
  set_tests_properties(ocr_image PROPERTIES TIMEOUT 60)

  add_executable(OffWorkAppEditTests
    tests/app_edit_tests.cpp
    src/app_edit.cpp src/app_view.cpp src/app_tray.cpp src/render.cpp
    src/settings.cpp src/autostart.cpp src/reminder.cpp src/tray_icon.cpp
    src/text_field.cpp src/layered_surface.cpp src/updater.cpp src/update_info.cpp
  )
  offwork_target_defaults(OffWorkAppEditTests)
  target_link_libraries(OffWorkAppEditTests PRIVATE OffWorkOcr
    advapi32 gdi32 msimg32 ole32 shell32 user32 winhttp)
  add_test(NAME app_edit COMMAND OffWorkAppEditTests)
  set_tests_properties(app_edit PROPERTIES TIMEOUT 15)

  enable_language(RC)
  # offwork.rc is UTF-8. rc.exe otherwise reads it in the system ANSI codepage,
  # which silently turns the Chinese FileDescription into mojibake in the
  # shipped binary -- worse than leaving Explorer properties blank.
  set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /c65001")
  add_executable(OffWork WIN32
    src/main.cpp
    src/app.cpp
    src/drop_target.cpp
    src/app_edit.cpp
    src/app_tray.cpp
    src/app_view.cpp
    src/autostart.cpp
    src/layered_surface.cpp
    src/reminder.cpp
    src/render.cpp
    src/settings.cpp
    src/text_field.cpp
    src/tray_icon.cpp
    src/updater.cpp
    src/update_info.cpp
    resources/offwork.rc
  )
  offwork_target_defaults(OffWork)
  target_link_libraries(OffWork PRIVATE
    OffWorkOcr
    advapi32
    comctl32
    dwmapi
    gdi32
    msimg32
    ole32
    shell32
    user32
    uxtheme
    winhttp
  )
endif()
