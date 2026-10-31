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
