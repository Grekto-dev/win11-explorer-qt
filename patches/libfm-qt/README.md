# libfm-qt progress-dialog seam overlay

- Upstream: libfm-qt 2.4.0, Git revision `4294f29ed314388f1f6d051ae0128320354e417a`.
- Patch: `0001-opt-in-progress-dialog.patch` (unified diff against `src/`).
- License: LGPL-2.1-or-later; unmodified upstream text is `LICENSE.LGPL-2.1` (SHA-256 `b3aa400aca6d2ba1f0bd03bd98d03d1fe7489a3bbb26969d72016360af8a5c9d`). Modified upstream files retain their original copyright/license notices. See the upstream checkout for AUTHORS and remaining notices.
- No SDK source or installed files are patched in place. `scripts/fidelity/build-libfm-overlay.sh` validates origin revision and hashes, copies to ignored `.state/overlay-source`, applies this patch, then builds/installs under `.state/libfm-overlay-*`.

SHA-256 of unmodified files at the revision above (also enforced by the build script):

```text
a90f13cc3e31d76ae173dc4b1af1897eb899c30c172a6d54576768787919e10f  src/fileoperation.h
aa4b6559c24c6307acf7a0720d63c639b842e8011b5107470733f11a3dd463f4  src/fileoperation.cpp
034b2de12c103c2d40ae7f3389655f9a4ef952059f431d70fbf4289beea33050  src/fileoperationdialog.h
6e60550d8cf3770484702b17c76a01b87b862a18ba9b03e461d8419b115d3c1e  src/core/job.h
e4783407320e82211b420118664e4df8bf76cbc016944df5176ac4616f6ae877  src/core/job.cpp
```

The patch adds an opt-in dialog factory; default construction remains upstream. It makes dialog presentation methods virtual and adds a pointer-response error signal for Qt queued delivery while preserving the pre-existing reference signal for source-level compatibility. This is an ABI change: rebuild the application and any binary consumers against the overlay; do not combine its headers with the SDK library.
