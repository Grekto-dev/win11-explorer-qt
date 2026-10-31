# Fidelity measurement and evidence format (schema v1)

This is a data contract, not a claim that evidence has been collected. The
reference operator owns Windows evidence under
`.state/fidelity/reference/phase2/<run-id>/` in the primary checkout.

## Manifest

A UTF-8 JSON manifest contains:

- `schema_version: 1`.
- `environment`: canonical keys are `build`, `theme`, `locale`, `dpi`, `scale`, `resolution`
  (`[width,height]`), `client_rect` (`[x,y,width,height]`), `refresh_hz`, and
  `capture_method`. Values describe the actual capture; do not fill unknowns
  with guesses. The validator losslessly adapts the published producer aliases
  `language`, `resolution_px`, `client_rect_px`, and
  `refresh_rate_hz_reported`; it records canonical fields in a normalized copy
  and retains provenance. Missing/unmeasured values become `unknown`, never
  inferred DPI, scale, client geometry, or refresh rate.
- `fixtures`: optional fixture metadata. If present, describe synthetic input,
  logical/allocated sizes, and hashes actually calculated.
- `cases`: non-empty list of unique `id`s with `status` exactly `measured`,
  `blocked`, or `not_observed`, plus `artifacts`, `measurements`, and
  `limitations`. Producer `measurements` may be a list or a keyed object and is
  normalized to a list in the returned copy. A `measured` case must contain at
  least one supporting artifact and one measurement; blocked/not-observed
  cases may be empty. Status is evidence availability, not equivalence.
- `frames`: optional list of `{file, timestamp_ms, region}` records. Timestamps
  are monotonic milliseconds from the capture clock and strictly increasing;
  `region` is `[x,y,width,height]` in local image pixels, not screen coordinates;
  x/y are non-negative integers and width/height positive integers. When image
  files are available, the ROI must fit each corresponding image. Screen-space
  input coordinates belong in measurements, not `region`. The tool reports
  pixel-change/MAE between adjacent ROIs and observed intervals; it does not
  interpolate frames or infer FPS or exact sub-cadence transition durations.
  For published producer frames, `elapsed_ms` is copied as-is and
  `monotonic_ns` is converted to milliseconds by the exact unit factor
  `1,000,000`; original fields and values remain under provenance. `region_px`
  is adapted to local-image `region` without changing its coordinates.

Relative paths are resolved from the manifest directory and must remain within
it. Producer absolute paths are accepted only when their resolved targets
remain inside the authorized `.state/fidelity/reference/phase2/` evidence tree.
Symlink traversal is checked after resolution. All paths must exist. Publish
manifests atomically after verifying all referenced files.
Use `python3 tools/fidelity/measure.py validate <manifest.json>` to check the
contract and artifacts. Unknown environment values must be represented as
explicit descriptive values only if the producer knows them; schema validation
requires a structurally valid container, not fabricated completeness. The
validator reports `quality_gaps` and `equality_eligible`; a valid container with
unknown environment values is not eligible for a parity/equality claim.

The published `transfer-ui-timeline.json` producer format is adapted explicitly:
its `elapsed_ms` values are preserved as measured timestamps. Its screenshots
are treated as whole-frame ROIs only when the log omits a region; dimensions are
read from each actual image, never interpreted as Explorer client geometry.
The adapter does not modify either source file.

## Image comparison report

Reports record UTC generation time, absolute evidence paths, SHA-256 hashes,
state identifiers, capture labels, Python/Pillow versions, and per-region
geometry, RGB mean absolute error/max channel error/changed-pixel count, alpha
error, and thresholded edge-mask disagreement. Edge threshold 32 is an
algorithm parameter for a descriptive measurement—not an acceptance
threshold. A crop is explicitly `NAME,X,Y,WIDTH,HEIGHT`; dimensions must match
unless an explicit crop can be applied to both images. Crop coordinates are
validated against both images.

A report uses `status: not_evaluated` and `equivalence: not_evaluated` unless
caller-supplied criteria are explicitly marked `status: measured`, cite a
`source` as `{ "path": ..., "sha256": ... }` identifying an existing evidence
file whose digest is verified, and provide one or more finite, non-negative
`max_*` limits for available metrics. Those
limits are preserved as supplied; the tool does not invent, relax, or infer
tolerances. Evaluation applies per region and records each observed value and
limit. `measured` describes criteria provenance/evaluation; `pass`/`fail`
describes only the explicitly bounded metrics, never universal visual parity.

Optional heatmap and overlay files are derived artifacts and do not alter
metrics. Preserve source captures unchanged. Synthetic test fixtures verify
algorithm behavior only and are not reference/Windows evidence.
