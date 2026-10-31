# SPDX-License-Identifier: GPL-2.0-or-later
find_package(Qt6Test ${QT_MINIMUM_VERSION} REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(tst_explorer tst_explorer.cpp)
target_link_libraries(tst_explorer PRIVATE pcmanfm-core Qt6::Test)
add_test(NAME explorer-ui COMMAND tst_explorer)
set_tests_properties(explorer-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=qt.qpa.*=false" TIMEOUT 90)
add_test(NAME isolation-contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_isolation.py)
add_test(NAME packaging-contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_packaging.py)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME cli-boundary COMMAND ${DBUS_RUN_SESSION} -- ${CMAKE_COMMAND} -E env
    "EXPLORER_BINARY=$<TARGET_FILE:win11-explorer-qt>"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.py)
set_tests_properties(cli-boundary PROPERTIES TIMEOUT 60)

# Separate executable owns its icon qrc; production core keeps its single qrc.
add_subdirectory(icons)
add_test(NAME fidelity-harness COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tests/fidelity -v)
add_test(NAME reference-capture-unit COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/scripts/fidelity/reference -p test_capture.py -v)
add_test(NAME selection-baseline COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/selection/probe_baseline.py)
set_tests_properties(fidelity-harness reference-capture-unit selection-baseline PROPERTIES TIMEOUT 60)

option(ENABLE_HEAVY_TRANSFER_TESTS "Build/register the real 2 GiB transfer test (opt-in; requires substantial free disk)" OFF)
if(ENABLE_HEAVY_TRANSFER_TESTS)
    set(LIBFMQT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/.deps/src/libfm-qt/src" CACHE PATH "Read-only libfm-qt source headers")
    set(LIBFMQT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/.deps/prefix/include/libfm-qt6" CACHE PATH "libfm-qt installed headers")
    if(NOT EXISTS "${LIBFMQT_SOURCE_DIR}/core/filetransferjob.h" OR NOT EXISTS "${LIBFMQT_INCLUDE_DIR}/fm-qt6_export.h")
        message(FATAL_ERROR "Heavy transfer test requires LIBFMQT_SOURCE_DIR and LIBFMQT_INCLUDE_DIR")
    endif()
    add_executable(tst_explorertransfercontroller
        ${PROJECT_SOURCE_DIR}/tests/transfer/tst_explorertransfercontroller.cpp
        ${PROJECT_SOURCE_DIR}/pcmanfm/explorertransfercontroller.cpp)
    target_include_directories(tst_explorertransfercontroller PRIVATE ${PROJECT_SOURCE_DIR}/pcmanfm ${LIBFMQT_SOURCE_DIR} ${LIBFMQT_INCLUDE_DIR})
    target_link_libraries(tst_explorertransfercontroller PRIVATE Qt6::Core Qt6::Test Qt6::Widgets fm-qt6)
    add_test(NAME transfer-controller-heavy COMMAND tst_explorertransfercontroller)
    set_tests_properties(transfer-controller-heavy PROPERTIES TIMEOUT 240 LABELS "heavy;transfer" RUN_SERIAL TRUE)
endif()
