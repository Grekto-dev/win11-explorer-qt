# Native Explorer frame recorder

`guest-recorder.ps1` uses C# `Graphics.CopyFromScreen` in the interactive Windows user session, buffers PNG frames in memory, and writes them after acquisition. It schedules pointer/key input in the same process and timestamps frames/events with one `Stopwatch` clock. The defaults record an Explorer toolbar/menu ROI for 11 seconds; change `-OutDir`, `-X/-Y/-W/-H`, or `-DurationMs` for later studies. It sends Escape before capture and waits `-DelayMs` (default 9 s) so the initial UI is clean and transient MCP control banners clear.

## VM capture procedure

1. Serve this directory from the Linux host on its private VM bridge (`192.168.122.1`) and fetch the script into `C:\Users\Public\ExplorerFidelityPhase2`.
2. Start it as the logged-in desktop user with `Start-Process powershell.exe -WindowStyle Hidden -ArgumentList ...`. Do **not** use a SYSTEM guest-agent process or a non-interactive scheduled task: GDI capture can return black frames there. Verify a non-black frame/contact sheet before publishing.
3. The recorder stores frames first and does no disk/network I/O in the timing loop. After it exits, zip/download the capture, extract it, and retain `guest-timeline.json` with each frame's monotonic `timestamp_ms` and filename.
4. Run `analyze_recording.py <timeline.json> <frames-dir> --roi <x1> <y1> <x2> <y2>`; it reports actual adjacent intervals and the luma-change series, without interpolating. Publish an atomic manifest only after checking paths and frame count.

The action IDs used by the older screenshot helper are filename tokens; `capture.py` rejects separators, traversal and invalid characters verbatim before making captures.

## Measured run

`/home/pedro/win11-explorer-qt/.state/fidelity/reference/phase2/20261031T0022-0300/manifest.json` is the current sample, with 152 PNG frames over 11.027 s, ROI `(480,240,640,400)`, actual median interval 65.228 ms (15.331 fps), min 43.082 ms, max 187.571 ms. It contains three timed repetitions each of menu and toolbar interactions. The achieved cadence is below the 30 fps target; timing precision is therefore one frame or worse. Frames were visually inspected; a first black-frame task-run trial was rejected and excluded.
