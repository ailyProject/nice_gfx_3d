#!/usr/bin/env node
// Real-asset loading and frame export, not an ESP32 benchmark or test suite.
import { readFile,writeFile,mkdir } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { fileURLToPath,pathToFileURL } from 'node:url';
import path from 'node:path';
import { ppmToPng } from './ppm-to-png.mjs';

const root=fileURLToPath(new URL('../../',import.meta.url));
let renderer=path.join(root,'examples','.cache',process.platform==='win32'?'render-model.exe':'render-model');
let previews=path.join(path.resolve(process.env.NICE_GFX_ARTIFACT_ROOT||'D:/temp/nice_gfx_artifacts'),'workspace/nice_gfx_3d/examples/previews');
const args=process.argv.slice(2);
for(let i=0;i<args.length;i++){
  if(args[i]==='--renderer'&&args[i+1])renderer=path.resolve(args[++i]);
  else if(args[i]==='--out'&&args[i+1])previews=path.resolve(args[++i]);
  else throw Error('Usage: node examples/tools/render-examples.mjs [--renderer path-to-render-model] [--out directory]');
}
await mkdir(previews,{recursive:true});
const sourceLink=file=>{const relative=path.relative(previews,file);return path.isAbsolute(relative)?pathToFileURL(file).href:relative.split(path.sep).join('/');};
const readmeLink=sourceLink(path.join(root,'examples','README.md')).replace(/&/g,'&amp;').replace(/"/g,'&quot;');
const groups=[
  {name:'Fox',clip:'Run',index:2,yaw:65,times:[0,.35,.7],source:'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Fox'},
  {name:'CesiumMan',clip:'animation_0',index:0,yaw:15,times:[0,.5,1],source:'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan'},
  {name:'RobotExpressive',clip:'Dance',index:0,yaw:20,times:[0,.7,1.4],source:'https://github.com/mrdoob/three.js/tree/dev/examples/models/gltf/RobotExpressive'},
  {name:'RobotExpressive',clip:'Jump',index:3,yaw:20,times:[0,.22,.45],source:'https://github.com/mrdoob/three.js/tree/dev/examples/models/gltf/RobotExpressive'},
];
const report={kind:'nice-gfx-3d-desktop-asset-loads',
  scope:'Actual portable C++ Model/AnimationMixer/Renderer loads and frames; no ESP32 compilation, board execution, or FPS measurement.',
  imageFormat:'Native RGB565 output converted to RGB8 PNG; 320x240 internal resolution.',groups:[]};
for(const group of groups){
  const asset=path.join(root,'examples','assets',group.name+'.ng3d'),input=await readFile(asset);
  const frames=[];
  for(let i=0;i<group.times.length;i++){
    const stem=group.name+'-'+group.clip+'-'+i,ppm=path.join(previews,stem+'.ppm'),png=path.join(previews,stem+'.png');
    const result=spawnSync(renderer,[asset,ppm,String(group.index),String(group.times[i]),String(group.yaw)],{cwd:root,encoding:'utf8',maxBuffer:1024*1024});
    if(result.error)throw result.error;
    if(result.status!==0)throw Error(`${stem}: native loader failed\n${result.stderr}\n${result.stdout}`);
    await ppmToPng(ppm,png);
    const statsLine=result.stdout.split(/\r?\n/).find(line=>line.startsWith('loaded='));
    frames.push({seconds:group.times[i],image:stem+'.png',native:statsLine,pngSha256:createHash('sha256').update(await readFile(png)).digest('hex')});
    process.stdout.write(stem+': '+statsLine+'\n');
  }
  report.groups.push({...group,asset:sourceLink(asset),assetBytes:input.length,assetSha256:createHash('sha256').update(input).digest('hex'),frames});
}
await writeFile(path.join(previews,'load-report.json'),JSON.stringify(report,null,2)+'\n');
const data=JSON.stringify(report.groups).replace(/</g,'\\u003c');
await writeFile(path.join(previews,'index.html'),`<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NICE_GFX_3D 真实模型加载预览</title>
<style>body{margin:0;background:#101722;color:#e0e8f4;font:15px/1.7 system-ui,sans-serif}main{max-width:1080px;margin:auto;padding:36px 24px}h1{font-size:26px;margin:0 0 12px}p{color:#aebbd0}a{color:#83c9ff}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px}article{background:#182231;padding:18px;border:1px solid #304059;border-radius:12px}h2{font-size:19px;margin:0 0 10px}img{display:block;width:100%;image-rendering:pixelated;background:#15202e;aspect-ratio:4/3;border-radius:6px}small{color:#aebbd0}button{margin:10px 10px 0 0;padding:7px 14px;background:#2b4a72;border:0;border-radius:5px;color:white;cursor:pointer}input{width:95%}code{font:12px monospace;word-break:break-word}</style>
<main><h1>真实模型与骨骼动画：桌面原生渲染采样</h1><p>这些图由本库 C++ Model / AnimationMixer / Renderer 读取 NG3D 后输出，非上游截图。每组展示三个实际动画时刻，网页每 0.45 秒切换一张采样帧。<strong>不是 ESP32 录屏或帧率测试。</strong>设备示例加载网格、贴图、骨骼和关键帧实时渲染，不使用这些图片播放动画。</p><section id="cards"></section><p><a href="load-report.json">实际加载与出图记录</a> · <a href="${readmeLink}">示例导航</a></p></main>
<script>const groups=${data};for(const group of groups){const card=document.createElement('article');let frame=0,playing=true;const title=document.createElement('h2');title.textContent=group.name+' / '+group.clip;const img=document.createElement('img');img.alt=title.textContent;const text=document.createElement('small');const slider=document.createElement('input');slider.type='range';slider.min=0;slider.max=group.frames.length-1;slider.value=0;const button=document.createElement('button');button.textContent='暂停切换';button.onclick=()=>{playing=!playing;button.textContent=playing?'暂停切换':'继续切换';};const source=document.createElement('a');source.href=group.source;source.textContent='上游来源';source.target='_blank';const draw=()=>{const selected=group.frames[frame];img.src=selected.image;slider.value=frame;text.textContent='采样时刻 '+selected.seconds.toFixed(2)+' 秒 · '+group.assetBytes.toLocaleString()+' bytes NG3D';};slider.oninput=()=>{frame=Number(slider.value);playing=false;button.textContent='继续切换';draw();};card.append(title,img,text,slider,button,source);document.getElementById('cards').append(card);draw();setInterval(()=>{if(playing){frame=(frame+1)%group.frames.length;draw();}},450);}</script></html>`);
process.stdout.write(`Wrote ${path.join(previews,'index.html')} and load-report.json. No ESP32 timing claims.\n`);
