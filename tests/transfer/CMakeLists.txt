cmake_minimum_required(VERSION 3.18)
project(transfer-controller-tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Test Widgets)
find_package(fm-qt6 2.4 REQUIRED CONFIG)
set(LIBFMQT_SOURCE_DIR "" CACHE PATH "libfm-qt source directory, needed for internal job headers")
set(LIBFMQT_INCLUDE_DIR "" CACHE PATH "libfm-qt installed headers directory")
if(NOT EXISTS "${LIBFMQT_SOURCE_DIR}/core/filetransferjob.h")
    message(FATAL_ERROR "Set LIBFMQT_SOURCE_DIR to libfm-qt/src")
endif()
if(NOT EXISTS "${LIBFMQT_INCLUDE_DIR}/fm-qt6_export.h")
    message(FATAL_ERROR "Set LIBFMQT_INCLUDE_DIR to the installed libfm-qt include directory")
endif()
add_executable(tst_explorertransfercontroller
    tst_explorertransfercontroller.cpp
    ../../pcmanfm/explorertransfercontroller.cpp
    ../../pcmanfm/explorertransferdialog.cpp
)
target_include_directories(tst_explorertransfercontroller PRIVATE
    ../../pcmanfm
    ${LIBFMQT_SOURCE_DIR}
    ${LIBFMQT_INCLUDE_DIR}
)
target_link_libraries(tst_explorertransfercontroller PRIVATE Qt6::Core Qt6::Test Qt6::Widgets fm-qt6)
enable_testing()
add_test(NAME transfer-controller COMMAND tst_explorertransfercontroller)
set_tests_properties(transfer-controller PROPERTIES TIMEOUT 180)
