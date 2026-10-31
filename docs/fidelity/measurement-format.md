# Fidelity measurement and evidence format (schema v1)

This is a data contract, not a claim that evidence has been collected. The
reference operator owns Windows evidence under
`.state/fidelity/reference/phase2/<run-id>/` in the primary checkout.

## Manifest

A UTF-8 JSON manifest contains:

- `schema_version: 1`.
- `environment`: `build`, `theme`, `locale`, `dpi`, `scale`, `resolution`
  (`[width,height]`), `client_rect` (`[x,y,width,height]`), `refresh_hz`, and
  `capture_method`. Values describe the actual capture; do not fill unknowns
  with guesses.
- `fixtures`: optional fixture metadata. If present, describe synthetic input,
  logical/allocated sizes, and hashes actually calculated.
- `cases`: non-empty list of unique `id`s with `status` exactly `measured`,
  `blocked`, or `not_observed`, plus `artifacts`, `measurements`, and
  `limitations` lists. Status is evidence availability, not equivalence.
- `frames`: optional list of `{file, timestamp_ms, region}` records. Timestamps
  are monotonic milliseconds from the capture clock and strictly increasing;
  `region` is `[x,y,width,height]`. The tool reports observed intervals and
  elapsed-time points; it does not interpolate frames or infer FPS.

All paths are relative to the manifest directory, must remain within it, and
must exist. Publish manifests atomically after verifying all referenced files.
Use `python3 tools/fidelity/measure.py validate <manifest.json>` to check the
contract and artifacts. Unknown environment values must be represented as
explicit descriptive values only if the producer knows them; schema validation
requires the fields, not a fabricated claim of measurement.

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
`source`, and provide one or more `max_*` limits for available metrics. Those
limits are preserved as supplied; the tool does not invent, relax, or infer
tolerances. Evaluation applies per region and records each observed value and
limit. `measured` describes criteria provenance/evaluation; `pass`/`fail`
describes only the explicitly bounded metrics, never universal visual parity.

Optional heatmap and overlay files are derived artifacts and do not alter
metrics. Preserve source captures unchanged. Synthetic test fixtures verify
algorithm behavior only and are not reference/Windows evidence.
