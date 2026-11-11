# Cesium Man — attribution and changes

Source: [Khronos glTF Sample Assets / CesiumMan](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/8f492390ddc7a2ae221a1369ef560589cf8c70fb/Models/CesiumMan)

Fixed source commit: `8f492390ddc7a2ae221a1369ef560589cf8c70fb`.

- Model, textures, rig and animation: Cesium, 2017, CC-BY-4.0.
- Cesium logo: Cesium, 2015; the upstream asset additionally includes the
  Cesium trademark notice. The CC license does not grant trademark rights.

Changes for this example: embedded JPEG texture resampled to a maximum edge
of 256 pixels and converted to RGBA PNG, then RGB565; nearest sampling and
base-color Lambert lighting replace desktop PBR material treatment. Converted
glTF data to NG3D. The original skeletal animation, hierarchy and weights are
retained. The unnamed animation receives the converter name `animation_0`.

See `UPSTREAM_LICENSE.md`, `UPSTREAM_metadata.json`, and `licenses/`, including
`LicenseRef-LegalMark-Cesium.txt`. These terms apply to the model and derived
asset data; the library's software license does not replace them. No endorsement by
Cesium or Khronos is implied. Source/output hashes are in `ASSET_METADATA.json`.
