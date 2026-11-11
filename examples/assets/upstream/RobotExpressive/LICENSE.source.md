# RobotExpressive source and attribution

The original model is by **Tomás Laulhé / Quaternius**. The upstream README
licenses it under **CC0 1.0**. Don McCurdy's listed modifications added three
facial expression morph targets, converted the source with FBX2glTF, removed
duplicate materials and reduced material metalness.

- Original creator: <https://www.patreon.com/quaternius>
- Contributor: <https://donmccurdy.com/>
- Official upstream directory: <https://github.com/mrdoob/three.js/tree/dev/examples/models/gltf/RobotExpressive>
- Pinned source commit: `b924f0cad4058dc4dde71445c796980c3cd5b5ed`
- Original model at that commit: <https://github.com/mrdoob/three.js/blob/b924f0cad4058dc4dde71445c796980c3cd5b5ed/examples/models/gltf/RobotExpressive/RobotExpressive.glb>
- Original README at that commit: <https://github.com/mrdoob/three.js/blob/b924f0cad4058dc4dde71445c796980c3cd5b5ed/examples/models/gltf/RobotExpressive/README.md>
- CC0 dedication: <https://creativecommons.org/publicdomain/zero/1.0/>

`README.upstream.md` is retained verbatim from the pinned commit. The original
GLB is also retained unchanged, with SHA-256 recorded in `source-lock.json`.
The CC0 asset attribution is separate from the software library's own license.

The NICE_GFX_3D example adaptation removes only facial morph target declarations,
default morph weights and animation channels targeting those morph weights.
It retains all original scene nodes, geometry, skin joints/inverse bind
matrices and translation/rotation/scale animation keys. PBR metalness and
roughness are approximated by the runtime's simple lighting. See
`../../RobotExpressive.metadata.json` for the per-clip adaptation report and
output hashes. The adapted files are derived example assets, not unchanged
upstream originals.
