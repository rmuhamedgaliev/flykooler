# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
project(FlyKooler
        VERSION 0.1.2
        DESCRIPTION "Control for Flydigi BS laptop coolers on KDE Plasma"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(FLYKOOLER_GUI "Build the graphical client (needs Qt6 Widgets)" ON)

# ---- Dependencies -----------------------------------------------------------
# Required: Qt 6 Core + Network (backend, CLI, shared code).
find_package(Qt6 6.4 COMPONENTS Core Network)
if(NOT Qt6_FOUND)
  message(FATAL_ERROR
    "Qt 6.4+ (Core, Network) was not found. Install it and re-run the build:\n"
    "  Arch / CachyOS : sudo pacman -S --needed qt6-base\n"
    "  Debian / Ubuntu: sudo apt install qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev\n"
    "  Fedora         : sudo dnf install qt6-qtbase-devel\n"
    "  openSUSE       : sudo zypper install qt6-base-devel")
endif()

# Optional: Qt6 DBus for logind's suspend/shutdown signals. Without it the
# backend still stops the cooler on SIGTERM, but not on suspend.
find_package(Qt6 6.4 OPTIONAL_COMPONENTS DBus)

# Optional: Qt6 Widgets for the GUI. Without it only the flykoolerd backend
# and the CLI are built — handy on headless machines.
if(FLYKOOLER_GUI)
  find_package(Qt6 6.4 COMPONENTS Widgets)
  if(NOT Qt6Widgets_FOUND)
    message(STATUS "Qt6 Widgets not found — GUI skipped, building flykoolerd only.")
    set(FLYKOOLER_GUI OFF)
  endif()
endif()

add_compile_definitions(FLYKOOLER_VERSION="${PROJECT_VERSION}")

# ---- Shared code: protocol, hidraw driver, sensors, config ----------------
set(FLYKOOLER_CORE
    src/Protocol.cpp
    src/Device.cpp
    src/Sensors.cpp
    src/Shared.cpp)

# ---- Backend: the single owner of the cooler -------------------------------
qt_add_executable(flykoolerd
    src/main_daemon.cpp
    src/Daemon.cpp
    ${FLYKOOLER_CORE})
target_link_libraries(flykoolerd PRIVATE Qt6::Core Qt6::Network)
if(Qt6DBus_FOUND)
  target_link_libraries(flykoolerd PRIVATE Qt6::DBus)
  target_compile_definitions(flykoolerd PRIVATE FLYKOOLER_DBUS)
else()
  message(STATUS "Qt6 DBus not found — no suspend hook through logind.")
endif()

# ---- GUI client and CLI ----------------------------------------------------
if(FLYKOOLER_GUI)
  find_package(Qt6 6.4 COMPONENTS LinguistTools)
  qt_add_executable(flykooler
      src/main.cpp
      src/MainWindow.cpp
      ${FLYKOOLER_CORE})
  target_link_libraries(flykooler PRIVATE Qt6::Widgets Qt6::Network)
  if(Qt6LinguistTools_FOUND)
    # UI translations, embedded into the binary at :/i18n (ru_RU included)
    qt_add_translations(flykooler TS_FILES translations/flykooler_ru.ts)
  endif()
endif()

# ---- Tests -------------------------------------------------------------------
# Pure logic: protocol framing/parsing against frames captured from hardware
# and the configuration/curve engine. Run with `make test`.
include(CTest)
if(BUILD_TESTING)
  find_package(Qt6 6.4 COMPONENTS Test)
  if(Qt6Test_FOUND)
    add_executable(tst_protocol tests/tst_Protocol.cpp src/Protocol.cpp)
    target_include_directories(tst_protocol PRIVATE src)
    target_link_libraries(tst_protocol PRIVATE Qt6::Test)
    add_test(NAME protocol COMMAND tst_protocol)

    add_executable(tst_config tests/tst_Config.cpp src/Shared.cpp)
    target_include_directories(tst_config PRIVATE src)
    target_link_libraries(tst_config PRIVATE Qt6::Test)
    add_test(NAME config COMMAND tst_config)
  endif()
endif()

# ---- Install ----------------------------------------------------------------
# The default CMAKE_INSTALL_PREFIX is /usr/local (sudo make install).
# For a user install: make install PREFIX=$HOME/.local
if(FLYKOOLER_GUI)
  install(TARGETS flykooler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES data/flykooler.desktop
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES data/flykooler.svg
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
endif()

install(TARGETS flykoolerd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE README.md
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/flykooler)

# ---- Packaging: make package / cpack -G DEB|RPM|TGZ ------------------------
# Distro integration (the udev rule and the systemd user unit) is placed by
# the packaging layer — see PKGBUILD — not by a plain `make install`.
set(CPACK_PACKAGE_NAME "flykooler")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Rinat Muhamedgaliev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Fan and lighting control for Flydigi BS laptop coolers")
set(CPACK_PACKAGE_DESCRIPTION
    "Client-server Qt 6 application for Flydigi BS-series laptop coolers: "
    "a temperature-driven fan curve, lighting and gear control over USB or "
    "Bluetooth, with a KDE Plasma tray client.")
set(CPACK_PACKAGE_CONTACT "Rinat Muhamedgaliev <rinat.muhamedgaliev@gmail.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
include(CPack)
