# The Qt 6 application. Compiler warnings here are on but not fatal: Qt's own headers are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
if(MSVC)
  add_compile_options(/W3 /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Qml Quick)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)

option(TC_DEV_TOOLS "Enable --preview and --grab (developer screenshots with sample data)" OFF)

set(TC_QML
  qml/qmldir qml/Tc.qml qml/Tx.qml qml/TcIcon.qml qml/Aurora.qml qml/GlassPlate.qml qml/GlowDot.qml qml/FocusRing.qml
  qml/TcButton.qml qml/IconButton.qml qml/GlassChip.qml qml/GlassCard.qml qml/GlassRow.qml qml/GlassSwitchRow.qml
  qml/RadioRow.qml qml/RowDivider.qml qml/SectionTitle.qml qml/Note.qml qml/Spacer.qml qml/TcField.qml qml/Headline.qml
  qml/BodyText.qml qml/Hero.qml qml/HeroBlock.qml qml/CodeBlock.qml qml/WaitingDots.qml qml/HeaderBar.qml qml/ScreenFrame.qml
  qml/TextButton.qml qml/Dialog.qml qml/RelayDialog.qml qml/NoticeCard.qml qml/AutoLockCard.qml qml/RelayCard.qml
  qml/UnlockScreen.qml qml/SetupScreen.qml qml/PairScreen.qml qml/EnterCodeScreen.qml qml/WaitingScreen.qml qml/VerifyScreen.qml
  qml/KeysResetScreen.qml qml/AboutScreen.qml qml/SettingsScreen.qml qml/SendButton.qml qml/ChatScreen.qml qml/Main.qml)

set(TC_APP_SOURCES src/main.cpp src/app_controller.cpp src/message_model.cpp src/platform.cpp src/qt_transport.cpp src/segmenter.cpp)
if(APPLE)
  list(APPEND TC_APP_SOURCES src/platform_mac.mm)
endif()

qt_add_executable(tenchar WIN32 MACOSX_BUNDLE ${TC_APP_SOURCES})
# Resource paths inside the binary: /qml/..., /fonts/..., /strings/...
qt_add_resources(tenchar tenchar_qml PREFIX "/qml" BASE "qml" FILES ${TC_QML})
qt_add_resources(tenchar tenchar_fonts PREFIX "/fonts" BASE "resources/fonts" FILES
  resources/fonts/Inter-Regular.ttf resources/fonts/Inter-Medium.ttf resources/fonts/Inter-SemiBold.ttf resources/fonts/Inter-Bold.ttf
  resources/fonts/NotoSansBengali-Regular.ttf resources/fonts/NotoSansBengali-Bold.ttf)
qt_add_resources(tenchar tenchar_strings PREFIX "/strings" BASE "resources/strings" FILES resources/strings/en.json resources/strings/bn.json)
target_link_libraries(tenchar PRIVATE tc_session Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Qml Qt6::Quick)
if(APPLE)
  target_link_libraries(tenchar PRIVATE "-framework AppKit")
endif()
if(TC_DEV_TOOLS)
  target_compile_definitions(tenchar PRIVATE TC_DEV_TOOLS)
endif()

# Tests that need Qt (the transport against a local HTTPS server). Needs sh, python3 and the openssl tool, so not on Windows.
if(NOT WIN32)
  add_executable(transport_test ../tests/transport_test.cpp src/qt_transport.cpp)
  target_link_libraries(transport_test PRIVATE tc_relayclient Qt6::Core Qt6::Network)
  target_include_directories(transport_test PRIVATE ../tests)
  add_test(NAME transport_test COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_transport_test.sh $<TARGET_FILE:transport_test>)
endif()
