set(pcmanfm_CORE_SRCS
    application.cpp
    mainwindow.cpp
    explorerchrome.cpp
    explorericons.cpp
    tabpage.cpp
    tabbar.cpp
    statusbar.cpp
    view.cpp
    launcher.cpp
    preferencesdialog.cpp
    xdgdir.cpp
    connectserverdialog.cpp
    settings.cpp
    bulkrename.cpp
    desktopentrydialog.cpp
)

qt6_add_dbus_adaptor(pcmanfm_DBUS_SRCS
    org.pcmanfm.Application.xml
    application.h
    PCManFM::Application
    applicationadaptor
    ApplicationAdaptor
)


# qt6_add_dbus_adaptor() already generated the moc files. It also marked the
# files with SKIP_AUTOMOC but we still need to mark them with SKIP_AUTOGEN.
# TODO: Check if this behaviour is a CMake bug.
set_source_files_properties(${pcmanfm_DBUS_SRCS} PROPERTIES SKIP_AUTOGEN ON)

set(pcmanfm_UIS
    main-win.ui
    about.ui
    shortcuts.ui
    preferences.ui
    connect.ui
    bulk-rename.ui
    desktopentrydialog.ui
)

# add translation for pcmanfm-qt
lxqt_translate_ts(QM_FILES
    UPDATE_TRANSLATIONS ${UPDATE_TRANSLATIONS}
    TEMPLATE pcmanfm-qt
    SOURCES ${pcmanfm_CORE_SRCS} ${pcmanfm_UIS}
    INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/win11-explorer-qt/translations"
)

add_library(pcmanfm-core STATIC
    ${pcmanfm_CORE_SRCS}
    ${pcmanfm_DBUS_SRCS}
    ${pcmanfm_UIS}
    ${QM_FILES}
)

file(GLOB_RECURSE EXPLORER_ICON_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/icons/explorer/*")
qt6_add_resources(pcmanfm-core explorer_icons
    PREFIX "/icons/win11-explorer-qt"
    BASE "${PROJECT_SOURCE_DIR}/icons/explorer"
    FILES ${EXPLORER_ICON_FILES})
install(FILES "${PROJECT_SOURCE_DIR}/icons/explorer/scalable/places/folder.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME win11-explorer-qt.svg)

target_include_directories(pcmanfm-core PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/pcmanfm-core_autogen/include
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pcmanfm-core
    PRIVATE
        PCMANFM_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/win11-explorer-qt"
        PCMANFM_QT_VERSION="${PCMANFM_QT_VERSION}"
        LIBFM_DATA_DIR="${PKG_FM_PREFIX}/share/libfm"
)

target_include_directories(pcmanfm-core
    PRIVATE
        "${Qt6Gui_PRIVATE_INCLUDE_DIRS}"
)

target_link_libraries(pcmanfm-core PUBLIC
    Qt6::Widgets
    Qt6::DBus
    fm-qt6
)

add_executable(win11-explorer-qt pcmanfm.cpp)
target_link_libraries(win11-explorer-qt PRIVATE pcmanfm-core)
set_target_properties(win11-explorer-qt PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
install(TARGETS win11-explorer-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES "${PROJECT_SOURCE_DIR}/packaging/win11-explorer-qt.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

# prevent the generated files from being deleted during make clean
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM true)
