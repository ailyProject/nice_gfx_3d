# Fox — attribution and changes

Source: [Khronos glTF Sample Assets / Fox](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/8f492390ddc7a2ae221a1369ef560589cf8c70fb/Models/Fox)

Fixed source commit: `8f492390ddc7a2ae221a1369ef560589cf8c70fb`.

- Model: PixelMannen, 2014, CC0-1.0.
- Rigging and animation: tomkranis, 2014, CC-BY-4.0.
- Conversion to glTF: @AsoboStudio and @scurest, 2017, CC-BY-4.0.

Changes for this example: texture resampled to a maximum edge of 256 pixels and
converted to RGBA PNG, then RGB565; missing normals generated as flat normals;
nearest texture filtering and base-color Lambert lighting replace desktop PBR
material treatment. Converted glTF data into NG3D. All original skin weights,
joint hierarchy and Survey/Walk/Run animation channels are retained.

See `UPSTREAM_LICENSE.md`, `UPSTREAM_metadata.json`, and the full license texts
under `licenses/`. Their permissions apply to the model and derived asset data;
the library's software license does not replace the asset's licenses. No endorsement
by the authors or Khronos is implied. Original/model hashes and output hashes
are recorded in `ASSET_METADATA.json`.
