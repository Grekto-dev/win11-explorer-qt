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
  `refresh_rate_hz_reported` (or `refresh_rate_hz`); it records canonical fields
  in a normalized copy and retains provenance. A present canonical field wins,
  including an explicit null; aliases are fallback only. An absent
  `refresh_rate_hz` does not erase canonical `refresh_hz`; an explicit null in
  that producer field means unknown when no canonical value exists and does not
  fall through to a reported rate. `refresh_rate_hz_reported` alone remains an
  untrusted-rate quality gap. Missing/unmeasured values become `unknown`, never
  inferred DPI, scale, client geometry, or refresh rate. Normalizing again
  preserves the original producer environment, source frames, timestamp units,
  and screen origins in the existing provenance, rather than wrapping copies.
  A supplied `provenance.producer_environment` must normalize to the same
  effective environment (including retained producer fields); incomplete or
  conflicting claims are rejected. Quality checks also inspect the effective
  input, so a claimed provenance cannot erase a reported-rate gap. This is
  contract consistency, not cryptographic attestation of the producer.
- `fixtures`: optional fixture metadata. If present, describe synthetic input,
  logical/allocated sizes, and hashes actually calculated.
- `artifacts`: optional top-level list of non-empty path strings (for example
  the source archive or contact sheet). All are validated, even when they do
  not also appear under a case or frame.
- `cases`: non-empty list of unique `id`s with `status` exactly `measured`,
  `partial`, `blocked`, or `not_observed`, plus `artifacts`, `measurements`, and
  `limitations`. A `measured` case must contain at least one supporting artifact
  and a real observed value. `partial` explicitly describes incomplete evidence,
  not a measured/equivalent result; partial/blocked/not-observed cases may retain
  empty containers or explicit unknowns without manufacturing values. Status is
  evidence availability, not equivalence.

  Measurements have two supported producer forms:

  - Named records: `[{"name":"duration_ms","value":0,"unit":"ms"}]`.
    Only the `value` payload of a named record counts as an observation. A
    missing `value`, even with `sample_count: 3`, cannot make the case measured.
    The `name`, `unit`, and `description` cannot make null/unknown payloads measured.
  - Raw keyed object: `{"duration_ms":0,"held":false,"visual_state":"idle"}`.
    Metric keys identify values; they are not observations themselves. The
    adapter preserves this object as `producer_measurements` and generates
    equivalent name/value records without dropping zero, false, null, or
    descriptive observed states. Nested payloads/lists may contain observations.
    The container determines the format: `{"name":"cronometro","value":null,
    "duration_ms":0,"held":false}` is still keyed and retains both observations;
    coincidental `name`/`value` keys do not turn it into a named record.

  Metadata keys `name`, `id`, `label`, `unit`, `units`, `description`, `source`,
  `provenance`, and `limitations` do not count as observed leaves. To measure a
  field with a reserved metadata name, use it as a metric name with a `value`
  payload. Null, empty containers/text, and explicit unknown/not-measured/
  not-observed markers (including underscore/hyphen spellings) are not evidence
  of an observation. Numeric zero and boolean false are valid observations.
  All numeric leaves in every case's measurements must be finite, including
  nested leaves and non-measured cases: a valid adjacent measure or textual
  identifier never masks NaN/Infinity. The original payloads are not rewritten
  to replace unknowns with numbers.
- `frames`: optional list of `{file, timestamp_ms, region}` records. Timestamps
  are monotonic milliseconds from the capture clock and strictly increasing;
  normalized `region` is `[x,y,width,height]` in local image pixels. List input
  is image-local; producer object input `{x,y,width,height}` is screen-space
  origin plus captured dimensions and is normalized to a local crop separately.
  x/y are non-negative integers and width/height positive integers. When image
  files are available, the ROI must fit each corresponding image. The tool reports
  pixel-change/MAE between adjacent ROIs and observed intervals; it does not
  interpolate frames or infer FPS or exact sub-cadence transition durations.
  When measured `input_events` are present in cases, the timeline report adds
  per-case event brackets and the next sampled ROI-change interval. These are
  temporal associations only; they do not compare distinct menu states as if
  they were equivalent static states or establish causality.
  For published producer frames, `elapsed_ms` is copied as-is and
  `monotonic_ns` is converted to milliseconds by the exact unit factor
  `1,000,000`; original fields and values remain under provenance. A supplied
  frame `timestamp_provenance` must be an object with exactly `field`,
  `source_value`, and `source_unit`: supported fields are `timestamp_ms` and
  `elapsed_ms` (ms), or `monotonic_ns` (ns). The source must be finite numeric
  data matching the canonical timestamp and the original field if still present.
  The original field may be absent in an already canonical frame; legitimate
  provenance is preserved across repeated normalization. Explicit null, malformed
  containers, unsupported units, and conflicting values are rejected with a
  controlled CLI error. Raw keyed and named-record `input_events` produce the
  same event associations after normalization. `region_px`
  is adapted to local-image `region` without changing its coordinates.
  A producer `region` object `{x,y,width,height}` is interpreted as an explicit
  screen-space ROI origin plus captured dimensions: the normalized image crop is
  `[0,0,width,height]`, and the screen origin is retained separately as
  `screen_origin`. A four-integer list remains image-local and never gains an
  inferred screen origin. Both forms are checked against each actual PNG; no
  coordinate bounds or integer checks are relaxed.
- Measured nested environment values are normalized without discarding their
  source: `dpi.GetDpiForWindow` becomes canonical `dpi`, `dpi.scale_percent`
  becomes unit scale, and `client_rect_px.client_rect` becomes canonical
  `client_rect`. A null refresh rate remains unknown and a quality gap. It does
  not prevent ROI pixels from being measured, but does not support a complete
  environment-parity or refresh-cadence claim.

Relative paths are resolved from the manifest directory and must remain within
it. Producer absolute paths are accepted only when their resolved targets
remain inside the authorized `.state/fidelity/reference/phase2/` evidence tree.
Symlink traversal is checked after resolution. Containment is enforced before
checking the resolved target's file existence or opening its contents. Top-level `artifacts`,
`cases[].artifacts`, and `frames[].file` all use the same resolver. File
existence/containment is checked when a base directory is supplied (the CLI
always supplies the manifest directory); in-memory validation without a base
cannot certify file existence. All referenced targets must be regular files. Publish
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

Report, heatmap, and overlay destinations are preflighted together, including
collisions between destinations (equal prefixes, PNG/report overlap, or repeated
region names that reuse a derived path). Resolved paths and existing file aliases
are checked before the first result write; rejected plans leave previous artifacts
intact. Symlink components, parent traversal, non-regular/multiply-linked targets,
source-file aliases, and writes into the recognized phase2 evidence root are
rejected. Each write uses an exclusive temporary file, no-follow directory
handles, and atomic replacement, rather than truncating the destination.
The opt-in real-evidence integration applies the same checks to both JSON
outputs before invoking the CLI. It inventories manifest/support/frame hashes
before and after, verifies PNG format, and requires 152 distinct resolved frame
paths, not 152 distinct content hashes (identical captures are legitimate).
Synthetic sandbox regressions exercise unsafe destinations; never point an
attack fixture at original Windows captures.
