# libfm-qt progress-dialog seam overlay

- Upstream: libfm-qt 2.4.0, Git revision `4294f29ed314388f1f6d051ae0128320354e417a`.
- Patch: `0001-opt-in-progress-dialog.patch`, `0002-cooperative-local-copy-pause.patch` and `0003-file-operation-data-safety.patch` (unified diffs against `src/`; apply in order).
- License: LGPL-2.1-or-later; unmodified upstream text is `LICENSE.LGPL-2.1` (SHA-256 `b3aa400aca6d2ba1f0bd03bd98d03d1fe7489a3bbb26969d72016360af8a5c9d`). Modified upstream files retain their original copyright/license notices. See the upstream checkout for AUTHORS and remaining notices.
- No SDK source or installed files are patched in place. `scripts/fidelity/build-libfm-overlay.sh` validates origin revision and hashes, copies to ignored `.state/overlay-source`, applies the three patches, then builds/installs under `.state/libfm-overlay-*`.
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
acea3dcdbcc6170d4af1647ff00bde37eb69818a7c1be23b716ff50ac2ac2531  src/utilities.cpp
57aae0bd1d79955e08703ef2c15dbb7c4515dd99a7b48cd9af684c42433ee08f  src/core/untrashjob.cpp
```

The patch adds an opt-in dialog factory; default construction remains upstream. It makes dialog presentation methods virtual and adds a pointer-response error signal for Qt queued delivery while preserving the pre-existing reference signal for source-level compatibility. This is an ABI change: rebuild the application and any binary consumers against the overlay; do not combine its headers with the SDK library.

Patch 0003 (data safety, audit FO-01..FO-07; regression: `tests/transfer/tst_fileops_safety.cpp`, CTest `fileops-safety`):
- Overwriting a NATIVE file (also when the existence probe is inconclusive) copies into a short private `.fm-<hex>.partial` sibling (exclusive create; a name collision picks another name) and renames it over the destination only when complete, so a failed write/ENOSPC leaves the original intact and the temp is removed on every exit path. A destination that is a symlink is REPLACED by the new regular file (as GIO did before the overlay); the link's target — even outside the folder or on another filesystem — is never written. The temp is created next to the destination's own path (no `realpath`) and renamed over it. The destination's `rwx` bits (`& 0777`) win over the source's (so private content can end up more permissive, as with `cp`); setuid/setgid/sticky are never carried over, and `chmod` is applied only to a regular temp file (a symlink source is copied as a link and its target's mode is never touched). Not preserved: owner/ACL/xattrs/SELinux context of the old file, and hardlinks to it (the other names keep the old content). Needs room for two copies (clear error on ENOSPC, original kept). In a folder where no sibling can be created (read-only/sticky) it falls back to the previous GIO in-place replace (which refuses with its own error there). Non-native destinations (smb/sftp/mtp) keep the plain in-place `g_file_copy(OVERWRITE)`: no FO-01 protection there.
- A destination is removed after a failed/cancelled copy only when the existence query returned `G_IO_ERROR_NOT_FOUND` explicitly before the copy AND the copy was not an OVERWRITE retry (exclusive create, so the file is provably ours); any other query error means "unknown" and nothing is deleted (the unreachable `err.reset()`-before-check cleanup in `handleError` is gone).
- Copy into the same folder creates `Nome - Cópia`, `Nome - Cópia (2)`…; move into the same folder is a silent no-op.
- Vanished source (copy/move) and missing destination folder (restore from trash) now emit a MODERATE error instead of failing silently.
- The "delete permanently instead of trash?" prompt defaults to No (also Escape).
- `UntrashJob` forwards the move subjob's `errorRequested` (the dialog only listens to that signal), so restore errors are really shown.
- `FileOperation::hadErrors()` (ABI addition; true only for severity >= MODERATE, i.e. what the dialog shows); `pasteFilesFromClipboard()` clears a cut selection only after the move finished without errors/cancel and the clipboard is unchanged.
- Not done: FO-05 (folder-over-folder move merge; "Ignorar" on a folder).
