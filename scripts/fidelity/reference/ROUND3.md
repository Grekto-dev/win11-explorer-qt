# Third-round reference capture

Run artifact directory: `/home/pedro/win11-explorer-qt/.state/fidelity/reference/phase2/20261031T0115-0300/`.

## Measured
- Full-display clean Explorer chrome captures: `toolbar-empty.png` (five visible synthetic items, none visibly highlighted) and `toolbar-selected.png` (real `Alpha.txt` selected; Explorer status bar says `1 item selecionado, 29 bytes`). Same `GestureFixture` window, display 1440x900.
- Real 2 GiB source `Large-2GiB.bin` copied via `Shell.Application.CopyHere` into two new task-owned destinations (`TransferDetails`, `TransferCancel`). Source and both destination sizes were 2,147,483,648 bytes; SHA-256 matched: `A7C744C13CC101ED66C29F672F92455547889CC586CE6D44FE76AE824958EA51`.
- Recorder run: 61 frames, 7,027.949 ms, 640x400 screen ROI `(400,300)`, 13.255 median achieved fps (45.391/75.446/718.510 ms min/median/max intervals); archive 268,365 bytes. It captured toolbar menu interactions, not a copy progress dialog.

## Not measured / limitations
- Empty-click/Ctrl-toggle/Shift-range, rectangle Escape/autoscroll edges, and DND default-vs-Ctrl effects were not completed/read back from `SelectedItems`; no DND filesystem effect is claimed.
- The 2 GiB copies completed before the recorder's first frames. No progress window/details/pause/resume/cancel buttons were observed or invoked; no cancellation partial-file behavior is claimed. A first UIA polling command had a PowerShell parser error. `INTERACTIONS_READY.json`, `TRANSFER_READY.json`, and `manifest.json` clearly mark remaining cases.
- Clipboard contents and per-control UIA `IsEnabled` were not obtained. 96 DPI/window geometry are inherited from the previous same-VM reference, not independently re-probed this round.

Recorder/test verification: `python3 -m unittest discover -s scripts/fidelity/reference -p 'test_*.py' -v` — 7 tests passed. Run manifests and artifacts are shared under `.state/fidelity/reference/phase2/20261031T0115-0300/`.
