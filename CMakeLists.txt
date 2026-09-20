cmake_minimum_required(VERSION 3.20)
project(Such VERSION 1.0.0 LANGUAGES CXX)


find_package(Threads REQUIRED)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SUCH_BUILD_GUI "Build the native GUI frontend" ON)
option(SUCH_BUILD_CLI "Build the CLI frontend" ON)
option(SUCH_BUILD_TESTS "Build public contract tests" ON)
option(SUCH_STRICT_WARNINGS "Treat compiler warnings as errors" ON)
option(SUCH_EXPERIMENTAL_LEADING_SWIPE "Enable experimental leading swipe actions" OFF)

# Generate the public version header from the CMake project version so product
# titles, package metadata and consumers cannot silently drift apart.
set(SUCH_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${SUCH_GENERATED_INCLUDE_DIR}/such")
configure_file(cmake/Version.h.in "${SUCH_GENERATED_INCLUDE_DIR}/such/Version.h" @ONLY)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckDesignContract.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckWindowsCompileHazards.cmake)

if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
endif()
if(WIN32)
  enable_language(RC)
endif()

function(such_strict target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /diagnostics:caret)
    if(SUCH_STRICT_WARNINGS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(SUCH_STRICT_WARNINGS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(such_apple_strict target)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
  if(SUCH_STRICT_WARNINGS)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

add_library(such_public_core STATIC
  src/cli/CliApp.cpp
  src/cli/ProcessControl.cpp
  src/ui/SearchDialect.cpp
  src/ui/IndexCommands.cpp
  src/ui/SwipeActions.cpp
  src/ui/ResponsiveLayout.cpp
  src/ui/DisplayScalePolicy.cpp
  src/ui/FrontendLease.cpp
  src/ui/ResultView.cpp
  src/ui/FontSettings.cpp
  src/ui/AgentLauncher.cpp
  src/runtime/RuntimeClient.cpp)
add_library(Such::Public ALIAS such_public_core)
set_target_properties(such_public_core PROPERTIES EXPORT_NAME Public)
target_compile_features(such_public_core PUBLIC cxx_std_20)
target_include_directories(such_public_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${SUCH_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(such_public_core PUBLIC Threads::Threads PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(such_public_core PRIVATE user32)
endif()
such_strict(such_public_core)

if(SUCH_BUILD_CLI)
  add_executable(such_cli apps/such_cli.cpp)
  set_target_properties(such_cli PROPERTIES OUTPUT_NAME "SuchCLI")
  target_link_libraries(such_cli PRIVATE Such::Public)
  such_strict(such_cli)
  install(TARGETS such_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(SUCH_BUILD_GUI)
  if(WIN32)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(SUCH_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Such.ico")
    configure_file(platform/windows/Such.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Such.rc" @ONLY)
    add_executable(such_windows WIN32 platform/windows/Win32Frontend.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/Such.rc")
    set_target_properties(such_windows PROPERTIES OUTPUT_NAME "Such")
    target_compile_definitions(such_windows PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00
      $<$<BOOL:${SUCH_EXPERIMENTAL_LEADING_SWIPE}>:SUCH_EXPERIMENTAL_LEADING_SWIPE=1>)
    target_link_libraries(such_windows PRIVATE Such::Public user32 gdi32 shell32 ole32)
    such_strict(such_windows)
    install(TARGETS such_windows RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_assets "${CMAKE_CURRENT_SOURCE_DIR}/platform/ipados/Assets.xcassets")
    add_executable(such_ipados MACOSX_BUNDLE platform/ipados/IPadFrontend.mm "${_assets}")
    set_source_files_properties("${_assets}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(such_ipados PROPERTIES OUTPUT_NAME "Such")
    target_link_libraries(such_ipados PRIVATE Such::Public "-framework UIKit" "-framework Foundation")
    target_compile_options(such_ipados PRIVATE -fobjc-arc)
    such_apple_strict(such_ipados)
    set_target_properties(such_ipados PROPERTIES
      OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES OBJCXX_EXTENSIONS NO
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "Such"
      MACOSX_BUNDLE_GUI_IDENTIFIER "net.heritage-labs.such"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "100"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/ipados/Info.plist.in"
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.heritage-labs.such"
      XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2"
      XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
      XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
    install(TARGETS such_ipados BUNDLE DESTINATION .)
  elseif(UNIX)
    find_package(X11 REQUIRED)
    find_package(PNG REQUIRED)
    add_executable(such_x11 platform/linux/X11Frontend.cpp platform/linux/LinuxNativeIcons.cpp platform/linux/LinuxIndexRoots.cpp)
    set_target_properties(such_x11 PROPERTIES OUTPUT_NAME "such")
    target_link_libraries(such_x11 PRIVATE Such::Public X11::X11 PNG::PNG)
    target_compile_definitions(such_x11 PRIVATE $<$<BOOL:${SUCH_EXPERIMENTAL_LEADING_SWIPE}>:SUCH_EXPERIMENTAL_LEADING_SWIPE=1>)
    such_strict(such_x11)
    install(TARGETS such_x11 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    configure_file(platform/linux/such.desktop.in "${CMAKE_CURRENT_BINARY_DIR}/such.desktop" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/such.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES assets/icon/linux/such-256.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps" RENAME such.png)
  endif()
endif()

if(SUCH_BUILD_TESTS)
  add_library(such_runtime_stub SHARED tests/fake_runtime.cpp)
  target_include_directories(such_runtime_stub PRIVATE include)
  target_compile_features(such_runtime_stub PRIVATE cxx_std_20)
  set_target_properties(such_runtime_stub PROPERTIES
    OUTPUT_NAME "SuchRuntimeStub"
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
  such_strict(such_runtime_stub)

  add_executable(such_public_contracts tests/public_contracts.cpp)
  target_link_libraries(such_public_contracts PRIVATE Such::Public)
  such_strict(such_public_contracts)

  add_executable(such_runtime_client_smoke tests/runtime_client_smoke.cpp)
  target_link_libraries(such_runtime_client_smoke PRIVATE Such::Public)
  such_strict(such_runtime_client_smoke)

  if(BUILD_TESTING)
    add_test(NAME such_public_contracts COMMAND such_public_contracts)
    add_test(NAME such_runtime_client_smoke
      COMMAND ${CMAKE_COMMAND} -E env "SUCH_RUNTIME_LIBRARY=$<TARGET_FILE:such_runtime_stub>" $<TARGET_FILE:such_runtime_client_smoke>)
    if(SUCH_BUILD_CLI)
      add_test(NAME such_cli_help COMMAND $<TARGET_FILE:such_cli> help)
      add_test(NAME such_cli_version COMMAND $<TARGET_FILE:such_cli> version)
      add_test(NAME such_cli_status
        COMMAND ${CMAKE_COMMAND} -E env "SUCH_RUNTIME_LIBRARY=$<TARGET_FILE:such_runtime_stub>" $<TARGET_FILE:such_cli> status)
      add_test(NAME such_cli_search
        COMMAND ${CMAKE_COMMAND} -E env "SUCH_RUNTIME_LIBRARY=$<TARGET_FILE:such_runtime_stub>" $<TARGET_FILE:such_cli> report)
    endif()
    add_test(NAME such_runtime_client_load_failure
      COMMAND ${CMAKE_COMMAND} -E env
        "SUCH_RUNTIME_LIBRARY=$<TARGET_FILE:such_runtime_stub>"
        "SUCH_FAKE_RUNTIME_FAIL_LOAD=1"
        $<TARGET_FILE:such_runtime_client_smoke> --expect-load-failure)
    add_test(NAME such_runtime_client_search_failure
      COMMAND ${CMAKE_COMMAND} -E env
        "SUCH_RUNTIME_LIBRARY=$<TARGET_FILE:such_runtime_stub>"
        "SUCH_FAKE_RUNTIME_FAIL_SEARCH=1"
        $<TARGET_FILE:such_runtime_client_smoke> --expect-search-failure)
    add_test(NAME such_runtime_client_roots_failure
      COMMAND ${CMAKE_COMMAND} -E env
        "SUCH_RUNTIME_LIBRARY=$<TARGET_FILE:such_runtime_stub>"
        "SUCH_FAKE_RUNTIME_FAIL_ROOTS=1"
        $<TARGET_FILE:such_runtime_client_smoke> --expect-roots-failure)
  endif()
endif()

install(TARGETS such_public_core EXPORT SuchPublicTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${SUCH_GENERATED_INCLUDE_DIR}/such/Version.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/such")

# Ship the public Apache-2.0 grant and attribution notice with every installed
# desktop payload. Release scripts package the install staging tree, so keeping
# this responsibility here prevents Windows/Linux/DEB packaging drift.
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/such")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.runtime/NOTICE.txt")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/.runtime/NOTICE.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/such/runtime"
    RENAME "HERITAGE_RUNTIME_NOTICE.txt")
endif()

set(_pkg "${CMAKE_INSTALL_LIBDIR}/cmake/SuchPublic")
configure_package_config_file(cmake/SuchPublicConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/SuchPublicConfig.cmake" INSTALL_DESTINATION "${_pkg}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/SuchPublicConfigVersion.cmake" VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(EXPORT SuchPublicTargets FILE SuchPublicTargets.cmake NAMESPACE Such:: DESTINATION "${_pkg}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SuchPublicConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/SuchPublicConfigVersion.cmake" DESTINATION "${_pkg}")
