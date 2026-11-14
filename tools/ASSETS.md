The shipped stream.nif and runoff.dds are original procedural assets. They do not
contain Bethesda or Community Shaders assets. The project license applies to the
assets and generator source. The optional nifly library used by the mesh generator
has its own GPL-3.0 license; it is an offline build dependency, not a plugin runtime
dependency. No nifly code is linked into RainRunoff.dll.

Rebuild the assets with:

```sh
cmake -S tools -B build-assets
cmake --build build-assets --config Release
build-assets/runoff_assets Data/meshes/RainRunoff/stream.nif
python tools/generate_texture.py
```

With Visual Studio's multi-configuration generator, the tool executable is in
`build-assets/Release/runoff_assets.exe`.

Validate the NIF without regenerating it:

```sh
build-assets/runoff_assets --validate Data/meshes/RainRunoff/stream.nif
```

The mesh is a NiNode named `RainRunoff`, with two crossed BSTriShape children,
`RainRunoff_RibbonX` and `RainRunoff_RibbonY`. Each has its own effect property.
Each ribbon contains 42 vertices and 40 triangles. In local space the stream starts
at z=0 and ends at z=-1. Maximum top width is 0.1, bottom width 0.012. Vertex alpha
fades the bottom out. The diffuse texture supplies streaks and gaps; the final
appearance must be assessed in Skyrim, especially at night and with other
transparency mods.

Both ribbons use BSEffectShaderProperty, ambient effect lighting, double-sided
rendering, depth testing, alpha blending, and no depth writes. The clamp mode is
clamp-S / wrap-T (1). The texture is RGBA8 with nine mip levels; the U borders
remain transparent at every mip level. The bottom two mip widths fade completely
to avoid a constant nonzero clamp color. No custom HLSL is used.

The plugin supplies V animation and U width control. For stream length L and
desired full-flow top width W, a uniform geometry scale L gives top width 0.1*L.
Set U scale to `max(1, 0.1*L/W)` and U offset to `0.5 - 0.5*scale` to narrow the
visible texture about the center. Apply the flow multiplier to W. Widths above
0.1*L need to be capped, since UV scaling cannot enlarge the geometry. Skip drawing
near zero flow rather than dividing by zero. V sampling is `4*t + offset`, so a
decreasing V offset makes streaks travel downward as t increases.

This uses the engine's existing effect shader path, which Community Shaders can
replace normally. It does not apply Community Shaders' horizontal water-surface
shader to a vertical plane. CS can perform its normal first-use shader compilation
and caching; tuning plugin width/timing values introduces no new shader source.

References used for the implementation:

- [nifly](https://github.com/ousnius/nifly/tree/40584be62399c64a3cb54fd2a68466e739932f39)
- [NIF format definitions](https://github.com/niftools/nifxml/blob/develop/nif.xml)
- [CS Effect shader](https://github.com/doodlum/skyrim-community-shaders/blob/dd2677fc4020db1da91b39cebef3dbfbe8913983/package/Shaders/Effect.hlsl)
- [CS Water shader](https://github.com/doodlum/skyrim-community-shaders/blob/dd2677fc4020db1da91b39cebef3dbfbe8913983/package/Shaders/Water.hlsl)
