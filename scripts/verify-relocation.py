#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import runpy, sys
from pathlib import Path
runpy.run_path(str(Path(__file__).resolve().with_name("verify_relocation.py")), run_name="__main__")
