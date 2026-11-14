# Validation record — 0.1.0 experimental source

## Performed in the Linux cloud workspace

- Configured and built the portable target through the supplied CMake
  `core-tests` preset. CTest passed its test executable, which performs 140
  checks. Tests cover rotated/subdivided roof geometry, deterministic eave
  selection, flat/tiny/invalid surfaces, timed ramps, rain blends, UV narrowing,
  and clearance strictly greater than player height.
- Independently compiled core/tests with GCC C++20, strict warnings treated as
  errors, AddressSanitizer, and UndefinedBehaviorSanitizer; all checks passed.
- Compiled Settings.cpp with strict GCC C++20 warnings; no diagnostics.
- Built the original NIF generator against pinned nifly. Loaded the shipped
  NIF back through nifly and verified SSE 20.2.0.7 / user 12 / BS 100, two
  crossed ribbons, 84 vertices, 80 triangles, alpha/depth/clamp flags, normalized
  width/length, vertex fade, and the texture reference.
- Parsed the shipped DDS header and every mip level. Verified dimensions,
  format masks, exact file size, complete mip chain, and transparent U borders.
- Checked engine API declarations against CommonLibSSE-NG
  b93280e832f263dbef44e44cbe2936622a02f91a. Reviewed main-thread task dispatch,
  independent effect materials, scene cleanup, player standing-height query,
  and local-axis collision sampling.

## Not performed

- No Windows MSVC compile or link of RainRunoff.dll. No DLL is included.
- The Windows GitHub Actions workflow has not been executed.
- No Skyrim launch, SKSE loading test, rendered visual inspection, CS integration
  test, live Havok raycast test, runtime-version compatibility test, or frame-time
  measurement. Header checks and portable tests do not establish these results.
- No universal roof coverage claim. Detector behavior on real vanilla and modded
  architecture must be assessed in-game; the geometry tests are synthetic.

## First acceptance criteria for a Windows build

Compile/package with the README commands, then complete its in-game verification
sequence. Inspect effect visibility/material isolation, correct corner placement
on yaw-rotated houses, independent clearance, weather transition timing, and
load/unload cleanup before calling this an install-ready release.

## Known limitations

- Two candidate points per static reference, not per semantic building. Broad
  category path hints and geometric evidence can still yield misses/false hits.
- Collision-only fallback is approximate. The perimeter walk is bounded and
  skips a corner if it cannot find an edge. Sheltered candidate edges may be
  skipped even when another edge of the same building would work.
- Clearance is cached until discovery is repeated. Moving statics or altered
  static collision need a reload or scan-radius exit/re-entry.
- Active roof slots are retained until the reference unloads or leaves the
  radius; a full pool can delay newly encountered closer buildings.
- Transparent ribbons have no physical water, splash particles, wet-ground
  decals, or water-specific CS refraction. Distant tiny streams may disappear
  as transparent mip levels are selected.
- Clamped-UV narrowing caps width to 10% of the drop length. A short eligible
  stream may therefore be narrower than the configured maximum.
