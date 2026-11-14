# RainRunoff — experimental SKSE plugin source

RainRunoff fakes water spilling from two roof corners during rain. It uses a
small animated, tapered, transparent mesh rather than fluid simulation. No
individual house meshes need editing or attachment markers.

**This is a source prototype, not an install-ready DLL release.** The portable
logic and generated asset files have been tested. The Windows plugin has not
been compiled here or tested in Skyrim. The included Windows build workflow is
provided for that next verification step; it has not been run for this package.

## Intended behavior

- Find nearby, loaded exterior static references whose model paths suggest
  buildings. Inspect upward-facing triangles and select two endpoints of a long,
  low eave. Use collision sampling if CPU geometry is unavailable.
- Check each corner independently. Its first static/terrain surface below must
  be **strictly farther away than the player's standing height**, including
  race and reference scale. Invalid player bounds fall back to 128 game units.
  The query ignores actors and dropped clutter. A ledge counts as the receiving
  surface: streams do not pass through it to reach terrain farther below.
- Reject outlets covered by another surface when `RequireSky=true`.
- Blend old/new rainy-weather classifications. Build from zero to full flow in
  about 8 seconds of established rain; drain to zero in 12 seconds after the
  weather becomes dry. Snow does not create new runoff. Times use active real
  seconds and freeze while the game is paused.
- At full rain, the default top width is 10 game units. The authored ribbon
  narrows to 12% of that width and becomes transparent near its end. As rain
  stops, its visible width shrinks everywhere without shrinking its length.
- End the effect just above the first receiving surface. The moving streak
  texture and fading tail imply breakup; there is no fluid or splash simulation.

Exactly two candidate outlets are chosen **per detected static reference**;
only the eligible corners render. A building assembled from multiple roof
references may produce more than two streams across the whole building.
The default budget is 24 active references / at most 48 streams within 6,000
world units. Distant LOD, interior architecture, and unloaded buildings are not
processed.

## Automatic roof detection

Skyrim has no reliable universal “this polygon is a roof corner” tag. Coverage
is heuristic, not a guarantee that every vanilla or modded roof will work.
Broad path hints are `architecture,house,roof,farm,building,hut,shack,dwelling`;
these are categories, not a list of targeted models. In the INI, add a category
for unusually named mod assets, or clear `IncludeTokens` to consider all statics
with a greater risk of false positives.

Simple sloped and flat roofs are the intended starting point. Complex shapes,
hipped/rounded roofs, decorative seams, multipart buildings, coarse collision,
missing collision, or unusual model transforms can cause misplaced or skipped
outlets. Geometry-derived locations are preferred. Collision-derived locations
are approximate and walk outward to the collision perimeter. Ground clearance
is recorded at discovery; changes to static collision without a 3D reload need
a save reload or leaving/re-entering the scan radius to rescan.

## Community Shaders

The effect uses Skyrim's existing `BSEffectShaderProperty` path, with original
NIF/DDS assets supplied here. Community Shaders can process that shader through
its normal rendering pipeline. **This does not use CS's actual water-surface
shader or promise its water-specific reflections/refraction.** That shader
assumes horizontal water and is unsuitable for vertical runoff without changes.

No CS headers, CS plugin dependency, custom HLSL, or CS recompilation is needed
for this implementation. Editing widths, opacity, or timings in the INI only
requires restarting Skyrim. CS can still do its usual first-use shader caching.
The effect also uses an ordinary engine shader when CS is absent.

## Runtime requirements

- Windows x64 Skyrim Special Edition / Anniversary Edition, an SKSE version
  matching the exact game executable, and the matching Address Library.
- Intended for the desktop SE/AE family, including 1.6.x; VR is disabled.
  Version independence uses CommonLibSSE-NG relocations rather than fixed hooks.
  This is not a tested compatibility claim for every 1.6.x release.
- Check `SkyrimSE.exe` Properties → Details for the full version. The estimate
  `1.6.170` may mean `1.6.1170`; do not select SKSE/Address Library by approximation.
- No ESP, Papyrus scripts, NIF replacements, ENB integration, or CS build required.

## Build once on Windows

Install Visual Studio 2022 with Desktop development with C++, a recent v143
compiler with C++23 support, Windows SDK, CMake 3.25+, Git, and vcpkg. Set the
`VCPKG_ROOT` environment variable to your vcpkg checkout. Dependencies and
CommonLib are pinned in `vcpkg.json` and `CMakeLists.txt`.

From a terminal in this directory:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel 2
ctest --preset windows-release
cmake --install build/windows --config Release --component Runtime --prefix build/package/Data
```

The resulting `build/package/Data` contains:

```text
SKSE/Plugins/RainRunoff.dll
SKSE/Plugins/RainRunoff.ini
meshes/RainRunoff/stream.nif
textures/RainRunoff/runoff.dds
```

Zip the `Data` folder for your mod manager. The GitHub Actions workflow does
these same build/test/package steps and uploads a package artifact when run in
a repository. The DLL itself needs compiling once per code change; ordinary
appearance tuning is through `RainRunoff.ini`.

## First in-game verification

Use an exterior with a simple farmhouse and confirm the plugin loaded in
`Documents/My Games/Skyrim Special Edition/SKSE/RainRunoff.log` (the actual SKSE
log directory can vary by game distribution). Enable `DebugLog=true` before
starting for per-reference detection messages.

1. Begin in clear weather: no streams should show.
2. Change to rainy weather with a weather mod or a known rainy weather FormID.
   Check that the outlets start near two eave corners, remain vertical on
   rotated buildings, and widen gradually.
3. Return to clear weather: check narrowing, fixed stream length, and complete
   disappearance after draining. Repeat with a gradual weather transition.
4. Inspect a low awning and a high roof. Only corners with clearance greater
   than the standing player's height should render. Crouch and walk beneath
   a stream to check that actor collision does not change its endpoint.
5. Enter/exit an interior, fast travel, and reload a save. Check cleanup and
   absence of duplicate streams. Test at night and with CS enabled/disabled.
6. Inspect complex/modded buildings and populated towns before raising the
   roof budget. Log detection misses and assess frame-time impact.

The plugin stores no savegame data. To remove it, disable its four installed
files through the mod manager and restart the game.

## Portable checks and asset sources

```sh
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

See `VALIDATION.md` for what was actually checked and what remains unverified.
Original mesh/texture generator sources and format notes are in
[tools/ASSETS.md](tools/ASSETS.md). The generator's nifly dependency is an offline
tool dependency, not part of the SKSE DLL.
