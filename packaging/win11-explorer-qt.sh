#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Launcher for the relocatable prefix. The real binary lives in ../libexec and
# finds its Qt/libfm-qt libraries through RUNPATH ($ORIGIN/../lib) and qt.conf,
# so no library or plugin search path is exported to the programs it opens.
# The Qt variables below would force a foreign Qt onto this binary; they are
# dropped for the file manager only (Fusion is requested on the command line,
# not through an environment variable, so children do not inherit it).
self=$(readlink -f -- "$0") || exit 1
here=${self%/*}
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
exec "$here/../libexec/win11-explorer-qt" -style Fusion "$@"
