#!/usr/bin/env node
// Asset preparation only. Does not compile firmware or run a renderer/test.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { compileGltf, cppHeader } from '../../tools/compile-model.mjs';

const COMMIT='b924f0cad4058dc4dde71445c796980c3cd5b5ed';
const ORIGINAL_SHA256='047f5e5fb3bb6d378bd1df16ca6137f2a596c99b3a1b5690b4020c05aaf6f319';
const SOURCE=`https://raw.githubusercontent.com/mrdoob/three.js/${COMMIT}/examples/models/gltf/RobotExpressive/`;
const examples=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const assets=path.join(examples,'assets'),upstream=path.join(assets,'upstream','RobotExpressive');
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
await fs.mkdir(upstream,{recursive:true});
if(process.argv.includes('--download')){
  for(const [remote,local] of [['RobotExpressive.glb','RobotExpressive.glb'],['README.md','README.upstream.md']]){
    const response=await fetch(SOURCE+remote);if(!response.ok)throw Error(`Download failed: ${response.status} ${remote}`);
    const bytes=new Uint8Array(await response.arrayBuffer());
    if(remote.endsWith('.glb')&&sha(bytes)!==ORIGINAL_SHA256)throw Error('Upstream GLB does not match the pinned SHA-256.');
    await fs.writeFile(path.join(upstream,local),bytes);
  }
}
const original=await fs.readFile(path.join(upstream,'RobotExpressive.glb'));
if(sha(original)!==ORIGINAL_SHA256)throw Error('Original GLB differs from the pinned upstream source; rerun with --download.');
const originalReadme=await fs.readFile(path.join(upstream,'README.upstream.md'));
if(original.readUInt32LE(0)!==0x46546c67||original.readUInt32LE(4)!==2||original.readUInt32LE(8)!==original.length)throw Error('Expected GLB 2.0.');
let sourceJson,bin;
for(let at=12;at<original.length;){const length=original.readUInt32LE(at),type=original.readUInt32LE(at+4);at+=8;const chunk=original.subarray(at,at+length);if(type===0x4e4f534a)sourceJson=JSON.parse(chunk.toString('utf8'));else if(type===0x004e4942)bin=chunk;at+=length;}
if(!sourceJson||!bin)throw Error('Expected JSON and BIN chunks.');
const adapted=structuredClone(sourceJson),changes={removedMorphTargets:0,removedMeshDefaultWeights:0,removedNodeDefaultWeights:0,removedMorphChannels:0,removedFacialOnlyClips:[],clips:[]};
for(const mesh of adapted.meshes||[]){
  if(mesh.weights!==undefined){delete mesh.weights;changes.removedMeshDefaultWeights++;}
  if(mesh.extras?.targetNames)delete mesh.extras.targetNames;
  for(const primitive of mesh.primitives||[])if(primitive.targets){changes.removedMorphTargets+=primitive.targets.length;delete primitive.targets;}
}
for(const node of adapted.nodes||[])if(node.weights!==undefined){delete node.weights;changes.removedNodeDefaultWeights++;}
adapted.animations=(adapted.animations||[]).filter(animation=>{
  const previous=animation.channels.length;
  animation.channels=animation.channels.filter(channel=>channel.target.path!=='weights');
  const removed=previous-animation.channels.length;changes.removedMorphChannels+=removed;
  changes.clips.push({name:animation.name,originalChannels:previous,retainedBodyChannels:animation.channels.length,removedMorphChannels:removed});
  if(!animation.channels.length){changes.removedFacialOnlyClips.push(animation.name);return false;}return true;
});
// Keep all nodes, skeletons, inverse bind matrices, geometry and TRS keyframes.
// Unused original morph accessors/BIN bytes remain in this adapted source GLB;
// the NG3D compiler writes only referenced runtime data.
const jsonBytes=Buffer.from(JSON.stringify(adapted)),jsonPadded=Buffer.alloc(Math.ceil(jsonBytes.length/4)*4,0x20);jsonBytes.copy(jsonPadded);
const binPadded=Buffer.alloc(Math.ceil(bin.length/4)*4);bin.copy(binPadded);
const bodyGlb=Buffer.alloc(12+8+jsonPadded.length+8+binPadded.length);
bodyGlb.writeUInt32LE(0x46546c67,0);bodyGlb.writeUInt32LE(2,4);bodyGlb.writeUInt32LE(bodyGlb.length,8);
bodyGlb.writeUInt32LE(jsonPadded.length,12);bodyGlb.writeUInt32LE(0x4e4f534a,16);jsonPadded.copy(bodyGlb,20);
const binAt=20+jsonPadded.length;bodyGlb.writeUInt32LE(binPadded.length,binAt);bodyGlb.writeUInt32LE(0x004e4942,binAt+4);binPadded.copy(bodyGlb,binAt+8);
const cooked=await compileGltf(new Uint8Array(bodyGlb));
const maxPrimitiveVertices=cooked.model.primitives.reduce((max,primitive)=>Math.max(max,primitive.vertexCount),3);
const modelObjects=cooked.model.nodes.length+cooked.model.nodes.reduce((sum,node)=>sum+(node.mesh>=0?cooked.model.meshes[node.mesh].primitiveCount:0),0)+5;
const report={
  name:'RobotExpressive',creator:'Tomas Laulhe / Quaternius',modifications:'Don McCurdy',license:'CC0-1.0',
  sourceRepository:'https://github.com/mrdoob/three.js',sourceCommit:COMMIT,
  original:{url:SOURCE+'RobotExpressive.glb',file:'upstream/RobotExpressive/RobotExpressive.glb',bytes:original.length,sha256:sha(original)},
  originalReadme:{url:SOURCE+'README.md',file:'upstream/RobotExpressive/README.upstream.md',bytes:originalReadme.length,sha256:sha(originalReadme)},
  adaptation:changes,clips:cooked.clips,warnings:cooked.warnings,
  counts:{vertices:cooked.model.vertices.length,triangles:cooked.model.indices.length/3,nodes:cooked.model.nodes.length,skins:cooked.model.skins.length,jointsPerSkin:cooked.model.skins.map(skin=>skin.joints.length),maxPrimitiveVertices,rendererObjectCapacity:modelObjects},
  outputs:{bodyGlb:{file:'RobotExpressive.body.glb',bytes:bodyGlb.length,sha256:sha(bodyGlb)},ng3d:{file:'RobotExpressive.ng3d',bytes:cooked.binary.length,sha256:sha(cooked.binary)}},
  preparationStatus:{assetPrepared:true,firmwareCompiled:false,boardTested:false,rendererLoaded:false},
};
await fs.writeFile(path.join(assets,'RobotExpressive.body.glb'),bodyGlb);
await fs.writeFile(path.join(assets,'RobotExpressive.ng3d'),cooked.binary);
await fs.writeFile(path.join(assets,'RobotExpressive.metadata.json'),JSON.stringify(report,null,2)+'\n');
await fs.writeFile(path.join(upstream,'source-lock.json'),JSON.stringify({sourceCommit:COMMIT,license:report.license,original:report.original,originalReadme:report.originalReadme},null,2)+'\n');
const sketch=path.join(examples,'RobotExpressivePlayer');await fs.mkdir(sketch,{recursive:true});
await fs.writeFile(path.join(sketch,'RobotExpressiveData.h'),cppHeader(cooked.binary,'robot_expressive_ng3d')+`\ninline constexpr uint32_t robot_expressive_max_vertices=${maxPrimitiveVertices}u;\ninline constexpr uint32_t robot_expressive_max_objects=${modelObjects}u;\n`,'utf8');
process.stdout.write(JSON.stringify({asset:'RobotExpressive',bytes:cooked.binary.length,counts:report.counts,clips:report.clips,removedMorphChannels:changes.removedMorphChannels,warnings:report.warnings,firmwareCompiled:false,boardTested:false},null,2)+'\n');
