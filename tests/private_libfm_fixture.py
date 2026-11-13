"""Shared physical I/O plan for private-libfm regression fixtures."""
from contextlib import contextmanager
from pathlib import Path
import shutil
import tempfile


def require_disjoint(output, overlay):
    output, overlay = Path(output).resolve(), Path(overlay).resolve()
    if output.is_relative_to(overlay) or overlay.is_relative_to(output):
        raise ValueError(f'fixture output overlaps input overlay: {output} / {overlay}')


def preflight(outputs, inputs):
    for index, output in enumerate(outputs):
        for protected in inputs:
            require_disjoint(output, protected)
        for other in outputs[:index]:
            require_disjoint(output, other)


@contextmanager
def fixture_plan(root, overlay, log_root):
    state = (root / '.state').resolve()
    overlay = overlay.resolve()
    # The allocation base may contain an existing input, but cannot be inside
    # it. Exclusive mkdir cannot alias or contain that existing input.
    if state.is_relative_to(overlay):
        raise ValueError('fixture state directory overlaps input overlay')
    state.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='private-libfm-', dir=state) as tmp:
        workspace = Path(tmp)
        try:
            preflight([workspace, log_root], [overlay])
        except ValueError:
            # Keep the selected input, not a naming restriction on overlays.
            # A fresh sibling log directory has independent ownership.
            require_disjoint(workspace, overlay)
            log_root = Path(tempfile.mkdtemp(prefix='libfm-logs-', dir=state))
        preflight([workspace, log_root], [overlay])
        logs = log_root / workspace.name
        logs.mkdir(parents=True, exist_ok=True)
        try:
            yield workspace, logs
        finally:
            # TemporaryDirectory removes only its fresh workspace, never logs.
            require_disjoint(workspace, overlay)
