#!/usr/bin/env node
// Asset acquisition/adaptation only. This script does not compile or test C++.
import { mkdir, readFile, writeFile, copyFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { compileGltf, cppHeader } from '../../tools/compile-model.mjs';

const here = fileURLToPath(new URL('./', import.meta.url));
const commit = '8f492390ddc7a2ae221a1369ef560589cf8c70fb';
const repository = 'https://github.com/KhronosGroup/glTF-Sample-Assets';
const rawRoot = `https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/${commit}/`;
const imageSize = 256;
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const downloaded = [];

async function acquire(relative, destination) {
  let bytes;
  try { bytes = await readFile(destination); }
  catch (error) {
    if (error.code !== 'ENOENT') throw error;
    const response = await fetch(rawRoot + relative, { headers: { 'User-Agent': 'NICE_GFX_3D-example-assets' } });
    if (!response.ok) throw new Error(`Download ${relative}: HTTP ${response.status}`);
    bytes = Buffer.from(await response.arrayBuffer());
    await mkdir(path.dirname(destination), { recursive: true });
    await writeFile(destination, bytes);
    process.stdout.write(`Downloaded ${relative}: ${bytes.length} bytes\n`);
  }
  downloaded.push({ path: relative, url: rawRoot + relative, bytes: bytes.length, sha256: hash(bytes) });
  return bytes;
}

function unpackGlb(bytes) {
  if (bytes.readUInt32LE(0) !== 0x46546c67 || bytes.readUInt32LE(4) !== 2) throw new Error('Expected original GLB 2.0');
  let json, bin;
  for (let at = 12; at < bytes.length;) {
    const size = bytes.readUInt32LE(at), type = bytes.readUInt32LE(at + 4), data = bytes.subarray(at + 8, at + 8 + size);
    if (type === 0x4e4f534a) json = JSON.parse(data.toString('utf8'));
    else if (type === 0x004e4942) bin = data;
    at += 8 + size;
  }
  if (!json || !bin) throw new Error('GLB JSON/BIN data missing');
  return { json, bin };
}

function multiply(a, b) {
  const out = new Array(16).fill(0);
  for (let c=0;c<4;c++) for (let r=0;r<4;r++) for (let k=0;k<4;k++) out[c*4+r] += a[k*4+r]*b[c*4+k];
  return out;
}
function compose(n) {
  const [x,y,z,w]=n.quaternion,[sx,sy,sz]=n.scale, [px,py,pz]=n.position;
  return [(1-2*(y*y+z*z))*sx,2*(x*y+w*z)*sx,2*(x*z-w*y)*sx,0,
    2*(x*y-w*z)*sy,(1-2*(x*x+z*z))*sy,2*(y*z+w*x)*sy,0,
    2*(x*z+w*y)*sz,2*(y*z-w*x)*sz,(1-2*(x*x+y*y))*sz,0,px,py,pz,1];
}
function transform(m, p) {
  return [0,1,2].map(r=>m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r]);
}
// Compute a useful camera frame from the cooked model's bind pose. This is
// authoring metadata, not a renderer validation or a performance measurement.
function bindBounds(model) {
  const worlds=[];
  const world=i=>worlds[i]??(worlds[i]=model.nodes[i].parent<0?compose(model.nodes[i]):multiply(world(model.nodes[i].parent),compose(model.nodes[i])));
  const low=[Infinity,Infinity,Infinity],high=[-Infinity,-Infinity,-Infinity];
  for (let ni=0;ni<model.nodes.length;ni++) {
    const node=model.nodes[ni];if(node.mesh<0)continue;
    let top=ni;while(model.nodes[top].parent>=0)top=model.nodes[top].parent;
    if(!model.roots.includes(top))continue;
    const mesh=model.meshes[node.mesh],skin=node.skin>=0?model.skins[node.skin]:null;
    const matrices=skin?skin.joints.map((joint,i)=>multiply(world(joint),skin.inverseBindMatrices[i])):null;
    for(let pi=mesh.firstPrimitive;pi<mesh.firstPrimitive+mesh.primitiveCount;pi++){
      const primitive=model.primitives[pi];
      for(let vi=0;vi<primitive.vertexCount;vi++){
        const position=model.vertices[primitive.vertexStart+vi].position;let p;
        if(skin){p=[0,0,0];const v=model.skinVertices[primitive.skinStart+vi];for(let k=0;k<4;k++){if(!v.weights[k])continue;const q=transform(matrices[v.joints[k]],position);for(let c=0;c<3;c++)p[c]+=q[c]*v.weights[k];}}
        else p=transform(world(ni),position);
        for(let c=0;c<3;c++){low[c]=Math.min(low[c],p[c]);high[c]=Math.max(high[c],p[c]);}
      }
    }
  }
  const center=low.map((v,i)=>(v+high[i])/2),radius=Math.hypot(...high.map((v,i)=>(v-low[i])/2));
  return { min:low, max:high, center, radius };
}

function infoHeader(name, result, bounds) {
  const maxVertices=Math.max(...result.model.primitives.map(p=>p.vertexCount));
  const objectBudget=result.model.nodes.length+result.model.primitives.length+16;
  const f=value=>{let text=Number(value).toPrecision(9);if(!text.includes('.')&&!/[eE]/.test(text))text+='.0';return text+'f';};
  return `#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace demo_asset {\ninline constexpr const char* name = "${name}";\ninline constexpr uint32_t maxVertices = ${maxVertices};\ninline constexpr uint32_t maxObjects = ${objectBudget};\ninline constexpr size_t bytes = ${result.binary.length};\ninline constexpr float centerX = ${f(bounds.center[0])};\ninline constexpr float centerY = ${f(bounds.center[1])};\ninline constexpr float centerZ = ${f(bounds.center[2])};\ninline constexpr float radius = ${f(bounds.radius)};\ninline constexpr size_t clipCount = ${result.clips.length};\n} // namespace demo_asset\n`;
}

async function prepare(name, exampleFolder, storage) {
  const upstream=path.join(here,'upstream',name),prepared=path.join(here,'prepared',name);
  await mkdir(prepared,{recursive:true});
  const original=await acquire(`Models/${name}/glTF-Binary/${name}.glb`,path.join(upstream,`${name}.glb`));
  for(const file of ['README.md','LICENSE.md','metadata.json'])await acquire(`Models/${name}/${file}`,path.join(upstream,file));
  const {json,bin}=unpackGlb(original);
  const changes=[];
  for(let i=0;i<(json.images||[]).length;i++){
    const image=json.images[i];if(image.bufferView===undefined)throw new Error(`${name}: expected an embedded original image`);
    const view=json.bufferViews[image.bufferView],data=bin.subarray(view.byteOffset||0,(view.byteOffset||0)+view.byteLength);
    const extension=image.mimeType==='image/jpeg'?'jpg':'png';
    const sourcePath=path.join(upstream,`texture_${i}.${extension}`),outputPath=path.join(prepared,`texture_${i}.png`);
    await writeFile(sourcePath,data);
    const converted=process.platform==='win32'
      ?spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(here,'resize-texture.ps1'),sourcePath,outputPath,String(imageSize)],{encoding:'utf8'})
      :spawnSync(process.env.PYTHON||'python3',[path.join(here,'resize-texture.py'),sourcePath,outputPath,String(imageSize)],{encoding:'utf8'});
    if(converted.status!==0)throw new Error(`Texture adaptation failed: ${converted.stderr||converted.error?.message||'Python/Pillow unavailable'}`);
    const png=await readFile(outputPath);
    image.uri='data:image/png;base64,'+png.toString('base64');image.mimeType='image/png';delete image.bufferView;
    changes.push(`Image ${i}: ${extension.toUpperCase()} converted to noninterlaced RGBA PNG; maximum edge ${imageSize}px, aspect ratio preserved.`);
  }
  for(const mesh of json.meshes||[])for(const primitive of mesh.primitives||[]){
    if(primitive.attributes.TANGENT!==undefined){delete primitive.attributes.TANGENT;changes.push('Unused tangent attribute removed; the embedded Lambert renderer does not use normal maps.');}
  }
  json.buffers[0].uri='data:application/octet-stream;base64,'+bin.subarray(0,json.buffers[0].byteLength).toString('base64');
  const adaptedPath=path.join(here,`${name}.gltf`);
  await writeFile(adaptedPath,JSON.stringify(json,null,2)+'\n');
  const result=await compileGltf(json),bounds=bindBounds(result.model);
  const info=infoHeader(name,result,bounds);
  await writeFile(path.join(here,`${name}.ng3d`),result.binary);
  const example=path.join(here,'..',exampleFolder);
  await mkdir(example,{recursive:true});
  if(storage==='flash') {
    await writeFile(path.join(example,`${name}Model.h`),cppHeader(result.binary,`k${name}Model`));
  } else {
    await mkdir(path.join(example,'data'),{recursive:true});
    await writeFile(path.join(example,'data',`${name}.ng3d`),result.binary);
  }
  await writeFile(path.join(example,'AssetInfo.h'),info);
  for(const file of ['README.md','LICENSE.md','metadata.json'])await copyFile(path.join(upstream,file),path.join(example,`UPSTREAM_${file}`));
  const metadata={
    name,repository,commit,source:rawRoot+`Models/${name}/glTF-Binary/${name}.glb`,originalSha256:hash(original),
    originalBytes:original.length,ng3dSha256:hash(result.binary),ng3dBytes:result.binary.length,
    vertices:result.model.vertices.length,indices:result.model.indices.length,triangles:result.model.indices.length/3,
    nodes:result.model.nodes.length,primitives:result.model.primitives.length,skins:result.model.skins.length,
    joints:result.model.skins.map(s=>s.joints.length),clips:result.clips,bounds,changes,warnings:result.warnings,
    animationPolicy:'All original skeletal animation channels and skin weights are retained. No animation was baked into frames or removed.',
    creationOnly:'Downloaded, adapted textures and converted assets. No C++ compilation, device test, or performance validation was run.',
  };
  await writeFile(path.join(here,`${name}.metadata.json`),JSON.stringify(metadata,null,2)+'\n');
  await writeFile(path.join(example,'ASSET_METADATA.json'),JSON.stringify(metadata,null,2)+'\n');
  process.stdout.write(JSON.stringify({name,ng3dBytes:metadata.ng3dBytes,vertices:metadata.vertices,triangles:metadata.triangles,joints:metadata.joints,clips:metadata.clips,bounds:metadata.bounds,warnings:metadata.warnings},null,2)+'\n');
}

await prepare('Fox','ModelFromFlash','flash');
await prepare('CesiumMan','ModelFromLittleFS','littlefs');
for(const license of ['CC-BY-4.0.txt','CC0-1.0.txt','LicenseRef-LegalMark-Cesium.txt']){
  const bytes=await acquire(`LICENSES/${license}`,path.join(here,'upstream','licenses',license));
  for(const example of ['ModelFromFlash','ModelFromLittleFS']){
    await mkdir(path.join(here,'..',example,'licenses'),{recursive:true});
    await writeFile(path.join(here,'..',example,'licenses',license),bytes);
  }
}
await writeFile(path.join(here,'Khronos.provenance.json'),JSON.stringify({repository,commit,files:downloaded},null,2)+'\n');
