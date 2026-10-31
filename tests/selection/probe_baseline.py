#!/usr/bin/env python3
"""Report selection/DND ownership and phase-2 reference availability.

This is a source-backed baseline probe, not a Windows behavior assertion.
It reads the checked-out app/libfm-qt sources and never modifies them.
"""
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LIBFM = REPO.parent.parent.parent / ".deps/src/libfm-qt/src/folderview.cpp"
APP_VIEW = REPO / "pcmanfm/view.cpp"
REFERENCE_ROOT = REPO.parent.parent.parent / ".state/fidelity/reference/phase2"


def main():
    lib = LIBFM.read_text()
    app = APP_VIEW.read_text()
    cases = [
        ("libfm_extended_selection", "setSelectionMode(QAbstractItemView::ExtendedSelection)" in lib, True),
        ("list_view_mouse_events_owned_by_libfm", "FolderViewListView::mousePressEvent" in lib and "FolderViewListView::mouseMoveEvent" in lib and "FolderViewListView::mouseReleaseEvent" in lib, True),
        ("details_rubberband_owned_by_libfm", "FolderViewTreeView::setSelection" in lib and "rubberBandRect_" in lib, True),
        ("file_dnd_callbacks_owned_by_libfm", "FolderView::childDropEvent" in lib and "FileOperation::copyFiles" in lib and "FileOperation::moveFiles" in lib, True),
        ("app_view_overrides_mouse_or_dnd_handlers", any(token in app for token in ("mousePressEvent", "mouseMoveEvent", "mouseReleaseEvent", "dropEvent")), False),
    ]
    manifests = sorted(str(p) for p in REFERENCE_ROOT.glob("*/INTERACTIONS_READY.json")) if REFERENCE_ROOT.exists() else []
    report = {
        "kind": "source-backed-baseline-probe",
        "reference_manifest_found": bool(manifests),
        "interaction_manifests": manifests,
        "checks": [{"id": name, "observed": observed, "expected": expected, "pass": observed == expected} for name, observed, expected in cases],
        "interpretation": "These checks identify implementation ownership only; they do not assert Windows semantics or runtime gesture results.",
    }
    print(json.dumps(report, indent=2))
    return 0 if all(observed == expected for _, observed, expected in cases) else 1


if __name__ == "__main__":
    raise SystemExit(main())
