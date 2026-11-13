# libfm-qt progress-dialog seam overlay

- Upstream: libfm-qt 2.4.0, Git revision `4294f29ed314388f1f6d051ae0128320354e417a`.
- Patch: `0001-opt-in-progress-dialog.patch` and `0002-cooperative-local-copy-pause.patch` (unified diffs against `src/`; apply in order).
- License: LGPL-2.1-or-later; unmodified upstream text is `LICENSE.LGPL-2.1` (SHA-256 `b3aa400aca6d2ba1f0bd03bd98d03d1fe7489a3bbb26969d72016360af8a5c9d`). Modified upstream files retain their original copyright/license notices. See the upstream checkout for AUTHORS and remaining notices.
- No SDK source or installed files are patched in place. `scripts/fidelity/build-libfm-overlay.sh` validates origin revision and hashes, copies to ignored `.state/overlay-source`, applies both patches, then builds/installs under `.state/libfm-overlay-*`.
- Patch 0002 keeps the pause/cancel gate in a `std::shared_ptr` that `Job::requestCancellation()` retains (together with the `GCancellable`) BEFORE waking the worker, so a QThreadPool autoDelete Job destroyed mid-call is never touched afterwards. The caller must still keep the Job alive until the call is entered. Regression: `tests/transfer/tst_job_cancel_lifetime.cpp` (needs `MALLOC_PERTURB_` and `GLIBC_TUNABLES=glibc.malloc.tcache_count=0`; CTest sets both).
- Patch 0002 adds checkpoint-based pause only for a single native regular-file COPY using `g_file_copy()` progress callbacks; it does not cover MOVE/LINK, directories, remote providers, or other job types. `LIBFM_QT_COOPERATIVE_COPY_PAUSE_API` is the compile-time capability/API marker. The overlay changes ABI: rebuild the application and any binary consumers against it; do not combine its headers with the SDK library.

```text
a90f13cc3e31d76ae173dc4b1af1897eb899c30c172a6d54576768787919e10f  src/fileoperation.h
aa4b6559c24c6307acf7a0720d63c639b842e8011b5107470733f11a3dd463f4  src/fileoperation.cpp
034b2de12c103c2d40ae7f3389655f9a4ef952059f431d70fbf4289beea33050  src/fileoperationdialog.h
6e60550d8cf3770484702b17c76a01b87b862a18ba9b03e461d8419b115d3c1e  src/core/job.h
e4783407320e82211b420118664e4df8bf76cbc016944df5176ac4616f6ae877  src/core/job.cpp
600ab7c6eb2fd5cb3e42fb92f46d37875b650f1f14848b90db4ebc23e0a9bda1  src/core/filetransferjob.h
49992c168502c59958a961432de5ee1fd5b126b6d0a566dc9a9793e820b1edaa  src/core/filetransferjob.cpp
4997bfe58f1ffcccdeae40ad3c96481c4ccf97a7dfd0835e9f1c30186fad9424  src/libfmqtglobals.h
```

The patch adds an opt-in dialog factory; default construction remains upstream. It makes dialog presentation methods virtual and adds a pointer-response error signal for Qt queued delivery while preserving the pre-existing reference signal for source-level compatibility. This is an ABI change: rebuild the application and any binary consumers against the overlay; do not combine its headers with the SDK library.
