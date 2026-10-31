"""Source/build-plan contracts for the fork's safety boundary."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def text(path):
    return (ROOT / path).read_text(encoding="utf-8")


class IsolationContracts(unittest.TestCase):
    def test_fork_executable_and_bus_identity_are_distinct(self):
        root = text("CMakeLists.txt")
        app_cmake = text("pcmanfm/CMakeLists.txt")
        app = text("pcmanfm/application.cpp")
        self.assertRegex(root, r"project\(win11-explorer-qt")
        self.assertRegex(app_cmake, r"add_executable\(win11-explorer-qt")
        self.assertIn('"org.grekto.Win11ExplorerQt"', app)
        self.assertIn('"win11-explorer-qt"', app)
        self.assertNotIn('"org.pcmanfm.PCManFM"', app)

    def test_filemanager1_service_is_not_registered_or_queued(self):
        app = text("pcmanfm/application.cpp")
        self.assertNotIn("org.freedesktop.FileManager1", app)
        self.assertNotIn("ApplicationAdaptorFreeDesktopFileManager", app)
        self.assertNotIn("QueueService", app)
        self.assertNotIn("applicationadaptorfreedesktopfilemanager", text("pcmanfm/CMakeLists.txt"))

    def test_desktop_and_wallpaper_cli_operations_are_rejected(self):
        app = text("pcmanfm/application.cpp")
        for option in ("desktop", "desktop-off", "desktop-pref", "set-wallpaper", "wallpaper-mode", "daemon-mode"):
            self.assertIn(f'QStringLiteral("{option}")', app)
        self.assertIn("Unsupported option", app)
        self.assertIn("std::exit(EXIT_FAILURE)", app)

    def test_desktop_dbus_operations_cannot_take_over_desktop(self):
        app = text("pcmanfm/application.cpp")
        xml = text("pcmanfm/org.pcmanfm.Application.xml")
        self.assertNotRegex(xml, r'<method name="(?:desktopManager|desktopPrefrences|setWallpaper)"')
        self.assertIn("void Application::desktopManager(bool", app)
        desktop_manager = app[app.index("void Application::desktopManager(bool"):app.index("void Application::desktopPrefrences")]
        self.assertIn("enableDesktopManager_ = false", desktop_manager)

    def test_runtime_volume_automation_is_disabled(self):
        app = text("pcmanfm/application.cpp")
        settings = text("pcmanfm/settings.cpp")
        self.assertNotIn("QTimer::singleShot(3000, this, &Application::initVolumeManager)", app)
        self.assertNotIn('g_signal_connect(volumeMonitor_, "volume-added"', app)
        self.assertRegex(settings, r"mountOnStartup_\(false\)")
        self.assertRegex(settings, r"mountRemovable_\(false\)")
        self.assertRegex(settings, r"autoRun_\(false\)")

    def test_no_layershell_and_only_fork_packaging_is_installed(self):
        root = text("CMakeLists.txt")
        app_cmake = text("pcmanfm/CMakeLists.txt")
        self.assertNotIn("LayerShellQt", root + app_cmake)
        self.assertIn("pcmanfm-core", app_cmake)
        self.assertIn("explorerchrome.cpp", app_cmake)
        self.assertNotIn("pcmanfm-qt.1", root)
        self.assertIn("packaging/win11-explorer-qt.desktop", app_cmake)
        self.assertNotIn("${DESKTOP_FILES}", app_cmake)

    def test_config_and_translation_paths_are_fork_scoped(self):
        app = text("pcmanfm/application.cpp")
        app_cmake = text("pcmanfm/CMakeLists.txt")
        settings = text("pcmanfm/settings.cpp")
        self.assertIn("win11-explorer-qt", app)
        self.assertIn("win11-explorer-qt", settings)
        self.assertIn("PCMANFM_DATA_DIR", app_cmake)
        self.assertIn("pcmanfm-qt_", app)
        self.assertIn("translations/pcmanfm-qt_pt_BR.ts", "\n".join(p.as_posix() for p in (ROOT / "pcmanfm/translations").glob("*.ts")))

    def test_profile_names_cannot_traverse(self):
        app = text("pcmanfm/application.cpp")
        settings = text("pcmanfm/settings.cpp")
        self.assertIn("safeProfile.match(profileName_)", app)
        self.assertIn("safeProfile.match(profile)", settings)
        self.assertIn("..", app)

    def test_reusable_core_and_ctest_are_configured(self):
        root = text("CMakeLists.txt")
        app_cmake = text("pcmanfm/CMakeLists.txt")
        self.assertIn("include(CTest)", root)
        self.assertRegex(root, r"option\(BUILD_TESTING")
        self.assertRegex(app_cmake, r"add_library\(pcmanfm-core STATIC")
        self.assertNotRegex(app_cmake, r"pcmanfm-core\s*\([^)]*pcmanfm\.cpp")
        self.assertIn("AUTOUIC", root)


if __name__ == "__main__":
    unittest.main()
