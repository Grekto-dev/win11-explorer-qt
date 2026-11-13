"""Shared physical I/O plan for private-libfm regression fixtures."""
from contextlib import contextmanager
from pathlib import Path
import shutil
import uuid


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
    root = Path(root).resolve()
    state = root / '.state'
    # .state is the sole output island in the source tree. All other source
    # entries (including the physical SDK behind .deps) are readonly inputs.
    inputs = [Path(overlay).resolve(), *(p.resolve() for p in root.iterdir()
                                      if p.name != '.state')]
    if state.is_symlink() or state.resolve() != state:
        raise ValueError('fixture state directory must not be a symlink')
    for protected in inputs:
        if state.is_relative_to(protected):
            raise ValueError('fixture state directory overlaps readonly input')
    workspace = state / ('private-libfm-' + uuid.uuid4().hex)
    logs = Path(log_root) / workspace.name
    # Plan every write before allocating anything. Legacy log names may be
    # legitimate inputs; retain the selection and use a fresh sibling instead.
    try:
        if not logs.resolve().is_relative_to(state):
            raise ValueError('logs outside the output island')
        preflight([workspace, logs], inputs)
    except ValueError:
        logs = state / ('libfm-logs-' + uuid.uuid4().hex)
    preflight([workspace, logs], inputs)
    state.mkdir(parents=True, exist_ok=True)
    workspace.mkdir()
    try:
        logs.mkdir(parents=True)
        yield workspace, logs
    finally:
        # No implicit TemporaryDirectory cleanup that could bypass this check.
        preflight([workspace], inputs)
        shutil.rmtree(workspace)
