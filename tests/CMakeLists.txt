# SPDX-License-Identifier: GPL-2.0-or-later
find_package(Qt6Test ${QT_MINIMUM_VERSION} REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(X11 REQUIRED)
add_executable(tst_explorer tst_explorer.cpp)
# Exercise scope cleanup during exception unwinding in this test executable.
target_compile_options(tst_explorer PRIVATE -fexceptions)
target_include_directories(tst_explorer PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(tst_explorer PRIVATE pcmanfm-core Qt6::Test ${X11_LIBRARIES})
add_test(NAME explorer-ui COMMAND tst_explorer)
set_tests_properties(explorer-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=qt.qpa.*=false;LD_LIBRARY_PATH=${_private_fm_prefix}/lib:$ENV{LD_LIBRARY_PATH}" TIMEOUT 90)
add_test(NAME isolation-contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_isolation.py)
add_test(NAME packaging-contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_packaging.py)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME cli-boundary COMMAND ${DBUS_RUN_SESSION} -- ${CMAKE_COMMAND} -E env
    "EXPLORER_BINARY=$<TARGET_FILE:win11-explorer-qt>"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.py)
set_tests_properties(cli-boundary PROPERTIES TIMEOUT 60 ENVIRONMENT "LD_LIBRARY_PATH=${_private_fm_prefix}/lib:$ENV{LD_LIBRARY_PATH}")

# Separate executable owns its icon qrc; production core keeps its single qrc.
add_subdirectory(icons)
add_test(NAME fidelity-harness COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tests/fidelity -v)
add_test(NAME reference-capture-unit COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/scripts/fidelity/reference -p test_capture.py -v)
add_test(NAME reference-capture-ids-unit COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/scripts/fidelity/reference -p test_capture_ids.py -v)
add_test(NAME reference-timeline-unit COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/scripts/fidelity/reference -p test_timeline.py -v)
set_tests_properties(reference-capture-ids-unit reference-timeline-unit PROPERTIES TIMEOUT 60)
add_test(NAME selection-wrapper-safety COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_selection_wrapper.py)
set_tests_properties(selection-wrapper-safety PROPERTIES ENVIRONMENT "EXPLORER_TEST_BINARY=$<TARGET_FILE:tst_explorer>;LD_LIBRARY_PATH=${_private_fm_prefix}/lib:$ENV{LD_LIBRARY_PATH}")
add_test(NAME selection-baseline COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/selection/probe_baseline.py)
set_tests_properties(fidelity-harness reference-capture-unit selection-baseline PROPERTIES TIMEOUT 60)

option(ENABLE_HEAVY_TRANSFER_TESTS "Build/register the real transfer integration tests (requires substantial free disk)" OFF)
if(ENABLE_HEAVY_TRANSFER_TESTS)
    set(LIBFMQT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/.state/overlay-source/libfm-qt/src" CACHE PATH "Matching patched libfm-qt overlay source headers")
    set(LIBFMQT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/.state/libfm-overlay-prefix/include/libfm-qt6" CACHE PATH "Matching private overlay installed headers")
    if(NOT EXISTS "${LIBFMQT_SOURCE_DIR}/core/filetransferjob.h" OR NOT EXISTS "${LIBFMQT_INCLUDE_DIR}/fm-qt6_export.h")
        message(FATAL_ERROR "Heavy transfer tests require a built private overlay; run scripts/fidelity/build-libfm-overlay.sh")
    endif()
    add_executable(tst_explorertransfercontroller
        ${PROJECT_SOURCE_DIR}/tests/transfer/tst_explorertransfercontroller.cpp
        ${PROJECT_SOURCE_DIR}/pcmanfm/explorertransfercontroller.cpp
        ${PROJECT_SOURCE_DIR}/pcmanfm/explorertransferdialog.cpp)
    target_include_directories(tst_explorertransfercontroller PRIVATE ${PROJECT_SOURCE_DIR}/pcmanfm ${LIBFMQT_SOURCE_DIR} ${LIBFMQT_INCLUDE_DIR})
    target_link_libraries(tst_explorertransfercontroller PRIVATE Qt6::Core Qt6::Test Qt6::Widgets fm-qt6)
    add_test(NAME transfer-controller-heavy COMMAND tst_explorertransfercontroller)
    set_tests_properties(transfer-controller-heavy PROPERTIES TIMEOUT 360 LABELS "heavy;transfer" RUN_SERIAL TRUE
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LD_LIBRARY_PATH=${_private_fm_prefix}/lib:$ENV{LD_LIBRARY_PATH}")

    # Deterministic Job cancellation lifetime regression. The executable
    # interposes pthread_cond_broadcast, so its symbols must be exported.
    find_package(Threads REQUIRED)
    add_executable(tst_job_cancel_lifetime
        ${PROJECT_SOURCE_DIR}/tests/transfer/tst_job_cancel_lifetime.cpp)
    set_target_properties(tst_job_cancel_lifetime PROPERTIES ENABLE_EXPORTS ON)
    target_include_directories(tst_job_cancel_lifetime PRIVATE ${LIBFMQT_SOURCE_DIR} ${LIBFMQT_INCLUDE_DIR})
    target_link_libraries(tst_job_cancel_lifetime PRIVATE Qt6::Core fm-qt6 Threads::Threads ${CMAKE_DL_LIBS})
    add_test(NAME job-cancel-lifetime COMMAND tst_job_cancel_lifetime)
    # MALLOC_PERTURB_ poisons freed Job memory; tcache_count=0 makes glibc really poison it
    # (tcache-recycled chunks are not), so a late access cannot pass by luck.
    set_tests_properties(job-cancel-lifetime PROPERTIES TIMEOUT 60 LABELS "heavy;transfer"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MALLOC_PERTURB_=165;GLIBC_TUNABLES=glibc.malloc.tcache_count=0;LD_LIBRARY_PATH=${_private_fm_prefix}/lib:$ENV{LD_LIBRARY_PATH}")
endif()
