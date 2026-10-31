# Fidelity harness

`tools/fidelity/measure.py` is an offline Python/Pillow CLI. It reads immutable
capture files and writes JSON reports; it does not operate the VM, capture a
screen, change production UI, or claim Windows equivalence without explicit
measured criteria. Python and Pillow are runtime requirements. The report
stores tool versions, absolute source paths, SHA-256 hashes, supplied state
identifiers, and raw per-region metrics.

## Compare static states

```sh
python3 tools/fidelity/measure.py compare \
  --reference /abs/reference.png --candidate /abs/linux.png \
  --reference-state initial-window --candidate-state initial-window \
  --reference-label 'Windows reference' --candidate-label 'Linux proof' \
  --region client,0,0,1104,642 \
  --report /abs/report.json
```

State identifiers must match. Images with different dimensions are rejected
unless an explicit region is supplied and fits in both images. The crop is
applied at identical image coordinates; compare equivalent client states and
regions. Add `--heatmap /abs/heatmap` and/or `--overlay /abs/overlay` for
optional per-region derived PNGs.

Without `--criteria`, the report remains `status: not_evaluated` and
`equivalence: not_evaluated`. Criteria are JSON, for example
`{"status":"measured","source":"repeatability measurements in run X","max_mae_rgb":1.2}`.
Every limit must be justified externally by observed capture precision and
repeatability; no default exists. This is not a claim of global or semantic
visual equivalence.

## Validate evidence and measure timelines

```sh
python3 tools/fidelity/measure.py validate /abs/run/manifest.json
python3 tools/fidelity/measure.py timeline /abs/run/manifest.json \
  --report /abs/timeline-report.json
```

Validation checks schema v1, statuses, required environment metadata, unique
case IDs, strictly increasing finite frame timestamps, frame regions, path
containment, and referenced evidence-file existence. The timeline report
contains actual elapsed timestamps and observed intervals, without FPS
interpolation. A manifest with zero frames cannot produce a timeline report.
Only the designated evidence-collection operator publishes Windows manifests;
this harness does not create replacement reference evidence.

## First real-image checkpoint

The currently available static inputs are the pre-existing
`.state/reference/explorer-window.png` and `.state/proof-x11/explorer-window.png`
from the primary checkout. They are both 1104x642, but the phase-2 reference
manifest directory may not yet contain a published run. A comparison between
these files is a descriptive initial checkpoint only: the reference image is
not a manifest-backed phase-2 run, the X11 capture is Linux evidence, and a
shared image size does not establish all environment/state equivalence. Do not
label this result Windows parity. When the designated operator publishes
`STATIC_READY.json`, validate that manifest and compare only matched measured
cases using their actual metadata and regions.

Synthetic images in the unit tests are explicitly algorithm fixtures, never
reported as Windows captures. Run `python3 -m unittest discover -s tests/fidelity -v`.
