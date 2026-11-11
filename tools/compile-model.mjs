#!/usr/bin/env node
// Shared browser/Node glTF compiler. Node-only dependencies are loaded by the CLI.
const LIMIT = 0x1000000;
const SECTIONS = [['VERT',36],['VSKN',24],['INDX',4],['MESH',8],['PRIM',32],['NODE',60],['MATL',28],['TEXR',24],['SKIN',24],['ANIM',16],['CHAN',24],['ROOT',4]];
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
const fail = message => { throw new Error(`NG3D: ${message}`); };
const integer = (v, name, max = 0xffffffff) => Number.isInteger(v) && v >= 0 && v <= max ? v : fail(`${name} must be an integer in [0, ${max}]`);
const finite = (v, name) => typeof v === 'number' && Number.isFinite(v) && Math.abs(v) <= 3.4028234663852886e38 ? v : fail(`${name} must be a finite float32`);
const array = (v, n, name, fallback) => {
  const a = v === undefined ? fallback : v;
  if (!Array.isArray(a) || a.length !== n) fail(`${name} must have ${n} components`);
  return a.map((x,i) => finite(x,`${name}[${i}]`));
};
const range = (start, count, length, name) => {
  integer(start,`${name} offset`); integer(count,`${name} length`);
  if (start > length || count > length - start) fail(`${name} is outside its buffer`);
};
const bytesOf = value => value instanceof Uint8Array ? value : value instanceof ArrayBuffer ? new Uint8Array(value) : ArrayBuffer.isView(value) ? new Uint8Array(value.buffer,value.byteOffset,value.byteLength) : fail('resource resolver must return bytes');
// Parent indices are validated by the caller. Each node is visited at most
// twice, so deeply nested authoring scenes do not cause quadratic import work.
function checkHierarchy(nodes, message) {
  const state = new Uint8Array(nodes.length);
  for (let i=0;i<nodes.length;i++) {
    if (state[i]===2) continue;
    let node=i;
    while (node!==-1&&state[node]===0) { state[node]=1; node=nodes[node].parent; }
    if (node!==-1&&state[node]===1) fail(message);
    node=i;
    while (node!==-1&&state[node]===1) { state[node]=2; node=nodes[node].parent; }
  }
}
const rgb565 = c => ((Math.round(Math.max(0,Math.min(1,c[0]))*31)<<11) | (Math.round(Math.max(0,Math.min(1,c[1]))*63)<<5) | Math.round(Math.max(0,Math.min(1,c[2]))*31));
const identity = () => [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
function checkInverseBind(m,label){
  if(Math.abs(m[3])+Math.abs(m[7])+Math.abs(m[11])+Math.abs(m[15]-1)>1e-5)fail(`${label}: inverse-bind matrix must be affine`);
  const determinant=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
  if(!Number.isFinite(determinant)||Math.abs(determinant)<1e-20)fail(`${label}: inverse-bind matrix must be invertible`);
}
function normalizedQuaternion(q, name) {
  const length = Math.hypot(...q);
  if (length < 1e-12) fail(`${name} has zero length`);
  return q.map(x => x / length);
}
function checkExtensions(value, path = 'glTF') {
  if (!value || typeof value !== 'object') return;
  if (value.extensions && Object.keys(value.extensions).length) fail(`${path}: unsupported extension ${Object.keys(value.extensions)[0]}`);
  if (value.extensionsUsed?.length || value.extensionsRequired?.length) fail(`${path}: glTF extensions are not supported (${[...(value.extensionsUsed||[]),...(value.extensionsRequired||[])].join(', ')})`);
  if (Array.isArray(value)) value.forEach((v,i)=>checkExtensions(v,`${path}[${i}]`));
  else for (const [k,v] of Object.entries(value)) if (k !== 'extras' && k !== 'extensions') checkExtensions(v,`${path}.${k}`);
}
function decodeDataUri(uri) {
  const match = /^data:([^,]*?),(.*)$/s.exec(uri);
  if (!match) fail('malformed data URI');
  if (/;base64$/i.test(match[1])) {
    let raw;
    try { raw = atob(match[2].replace(/\s/g,'')); } catch { fail('malformed base64 data URI'); }
    return Uint8Array.from(raw,c=>c.charCodeAt(0));
  }
  const out=[];
  for(let i=0;i<match[2].length;i++){
    if(match[2][i]==='%'){
      const hex=match[2].slice(i+1,i+3);if(!/^[0-9A-Fa-f]{2}$/.test(hex))fail('malformed percent-encoded data URI');
      out.push(parseInt(hex,16));i+=2;
    }else{const code=match[2].charCodeAt(i);if(code>127)fail('non-ASCII data URI bytes must be percent-encoded');out.push(code);}
  }
  return Uint8Array.from(out);
}
async function sourceDocument(input) {
  if (typeof input === 'string') { try { return { json:JSON.parse(input), bin:null }; } catch { fail('invalid glTF JSON'); } }
  if (input && typeof input === 'object' && !ArrayBuffer.isView(input) && !(input instanceof ArrayBuffer)) return { json:input,bin:null };
  const bytes = bytesOf(input);
  const view = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if (bytes.length >= 4 && view.getUint32(0,true) === 0x46546c67) {
    if (bytes.length < 20 || view.getUint32(4,true)!==2 || view.getUint32(8,true)!==bytes.length) fail('invalid GLB 2.0 header');
    let json = null, bin = null, offset = 12, chunk = 0;
    while (offset < bytes.length) {
      range(offset,8,bytes.length,'GLB chunk header');
      const length=view.getUint32(offset,true), type=view.getUint32(offset+4,true); offset+=8;
      if (length%4) fail('GLB chunks must be four-byte aligned');
      range(offset,length,bytes.length,'GLB chunk');
      const data=bytes.subarray(offset,offset+length);
      if (type===0x4e4f534a) {
        if (json || chunk!==0) fail('GLB must start with one JSON chunk');
        try { json=JSON.parse(decoder.decode(data)); } catch { fail('invalid GLB JSON'); }
      } else if (type===0x004e4942) {
        if (bin) fail('GLB has multiple BIN chunks'); bin=data;
      } else fail('unsupported GLB chunk type');
      offset+=length; chunk++;
    }
    if (!json) fail('GLB JSON is missing');
    return {json,bin};
  }
  try { return {json:JSON.parse(decoder.decode(bytes)),bin:null}; } catch { fail('input is neither GLB 2.0 nor glTF JSON'); }
}
async function inflatePng(bytes) {
  if (typeof DecompressionStream !== 'undefined') {
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }
  if (typeof process !== 'undefined' && process.versions?.node) {
    const moduleName='node:zlib'; const {inflateSync}=await import(/* @vite-ignore */ moduleName);
    return new Uint8Array(inflateSync(bytes));
  }
  fail('PNG deflate decoder unavailable in this browser');
}
async function decodePng(bytes) {
  const sig=[137,80,78,71,13,10,26,10];
  if (bytes.length<33 || !sig.every((x,i)=>bytes[i]===x)) fail('only PNG textures are supported');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const crcTable=Uint32Array.from({length:256},(_,n)=>{for(let k=0;k<8;k++)n=(n&1)?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
  let offset=8,width=0,height=0,type=-1,palette=null,transparency=null,seenHeader=false,seenEnd=false;
  const blocks=[];
  while (offset<bytes.length) {
    range(offset,12,bytes.length,'PNG chunk');
    const size=view.getUint32(offset,false), tag=String.fromCharCode(...bytes.subarray(offset+4,offset+8));
    range(offset+8,size+4,bytes.length,'PNG chunk data');
    let crc=0xffffffff;for(let i=offset+4;i<offset+8+size;i++)crc=crcTable[(crc^bytes[i])&255]^(crc>>>8);
    if(((crc^0xffffffff)>>>0)!==view.getUint32(offset+8+size,false))fail(`PNG ${tag} checksum is invalid`);
    const data=bytes.subarray(offset+8,offset+8+size);
    if (!seenHeader && tag!=='IHDR') fail('PNG IHDR must be first');
    if (tag==='IHDR') {
      if (seenHeader || size!==13) fail('invalid PNG IHDR'); seenHeader=true;
      width=view.getUint32(offset+8,false); height=view.getUint32(offset+12,false); type=data[9];
      if (!width || !height || width>4096 || height>4096 || width*height>LIMIT) fail('PNG dimensions exceed 4096 or pixel budget');
      if (data[8]!==8 || ![0,2,3,4,6].includes(type) || data[10]!==0 || data[11]!==0 || data[12]!==0) fail('PNG requires noninterlaced eight-bit standard compression/filtering');
    } else if (tag==='PLTE') { if (!size || size%3 || size>768) fail('invalid PNG palette'); palette=data.slice(); }
    else if (tag==='tRNS') transparency=data.slice();
    else if (tag==='IDAT') blocks.push(data);
    else if (tag==='IEND') { if (size) fail('invalid PNG IEND'); seenEnd=true; offset+=12; break; }
    else if (tag[0]===tag[0].toUpperCase()) fail(`unsupported critical PNG chunk ${tag}`);
    offset+=size+12;
  }
  if (!seenEnd || !blocks.length || offset!==bytes.length) fail('incomplete PNG');
  if (type===3 && !palette) fail('indexed PNG has no palette');
  if (transparency && ((type===0 && transparency.length!==2)||(type===2 && transparency.length!==6)||(type===3 && transparency.length>palette.length/3)||type===4||type===6)) fail('invalid PNG transparency');
  const channels=({0:1,2:3,3:1,4:2,6:4})[type], stride=width*channels;
  const compressed=new Uint8Array(blocks.reduce((n,b)=>n+b.length,0)); let p=0;
  for (const b of blocks) {compressed.set(b,p);p+=b.length;}
  const raw=await inflatePng(compressed);
  if (raw.length!==(stride+1)*height) fail('PNG decompressed byte count is invalid');
  const image=new Uint8Array(stride*height);
  const paeth=(a,b,c)=>{const q=a+b-c,pa=Math.abs(q-a),pb=Math.abs(q-b),pc=Math.abs(q-c);return pa<=pb&&pa<=pc?a:pb<=pc?b:c;};
  for(let y=0;y<height;y++) {
    const filter=raw[y*(stride+1)]; if(filter>4) fail('invalid PNG row filter');
    for(let x=0;x<stride;x++) {
      const i=y*stride+x, left=x>=channels?image[i-channels]:0, up=y?image[i-stride]:0, diagonal=y&&x>=channels?image[i-stride-channels]:0;
      image[i]=(raw[y*(stride+1)+1+x]+(filter===0?0:filter===1?left:filter===2?up:filter===3?Math.floor((left+up)/2):paeth(left,up,diagonal)))&255;
    }
  }
  const pixels=new Uint16Array(width*height),alpha=new Uint8Array(width*height);let opaque=true;
  for(let i=0;i<pixels.length;i++) {
    const at=i*channels;let r,g,b,a=255;
    if(type===0||type===4) {r=g=b=image[at]; if(type===4)a=image[at+1]; else if(transparency&&r===((transparency[0]<<8)|transparency[1]))a=0;}
    else if(type===2||type===6) {r=image[at];g=image[at+1];b=image[at+2];if(type===6)a=image[at+3];else if(transparency&&r===((transparency[0]<<8)|transparency[1])&&g===((transparency[2]<<8)|transparency[3])&&b===((transparency[4]<<8)|transparency[5]))a=0;}
    else {const index=image[at];if(index*3+2>=palette.length)fail('PNG palette index out of range');r=palette[index*3];g=palette[index*3+1];b=palette[index*3+2];a=transparency&&index<transparency.length?transparency[index]:255;}
    pixels[i]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);alpha[i]=a;if(a!==255)opaque=false;
  }
  return {width,height,pixels,alpha:opaque?null:alpha};
}
function decomposeNode(node,index) {
  if(!node.matrix) return {position:array(node.translation,3,`node ${index} translation`,[0,0,0]),quaternion:normalizedQuaternion(array(node.rotation,4,`node ${index} rotation`,[0,0,0,1]),`node ${index} rotation`),scale:array(node.scale,3,`node ${index} scale`,[1,1,1])};
  if(node.translation||node.rotation||node.scale)fail(`node ${index} combines matrix and TRS`);
  const m=array(node.matrix,16,`node ${index} matrix`);
  if(Math.abs(m[3])+Math.abs(m[7])+Math.abs(m[11])+Math.abs(m[15]-1)>1e-5)fail(`node ${index} matrix is not affine`);
  const determinant=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
  const scale=[Math.hypot(m[0],m[1],m[2])*(determinant<0?-1:1),Math.hypot(m[4],m[5],m[6]),Math.hypot(m[8],m[9],m[10])];
  if(scale.some(s=>Math.abs(s)<1e-12))fail(`node ${index} matrix has zero scale; use TRS instead`);
  const r=[m[0]/scale[0],m[1]/scale[0],m[2]/scale[0],m[4]/scale[1],m[5]/scale[1],m[6]/scale[1],m[8]/scale[2],m[9]/scale[2],m[10]/scale[2]];
  if(Math.abs(r[0]*r[3]+r[1]*r[4]+r[2]*r[5])+Math.abs(r[0]*r[6]+r[1]*r[7]+r[2]*r[8])+Math.abs(r[3]*r[6]+r[4]*r[7]+r[5]*r[8])>1e-4)fail(`node ${index} matrix has unsupported shear`);
  const trace=r[0]+r[4]+r[8];let q;
  if(trace>0){const s=2*Math.sqrt(trace+1);q=[(r[5]-r[7])/s,(r[6]-r[2])/s,(r[1]-r[3])/s,s/4];}
  else if(r[0]>r[4]&&r[0]>r[8]){const s=2*Math.sqrt(1+r[0]-r[4]-r[8]);q=[s/4,(r[3]+r[1])/s,(r[6]+r[2])/s,(r[5]-r[7])/s];}
  else if(r[4]>r[8]){const s=2*Math.sqrt(1+r[4]-r[0]-r[8]);q=[(r[3]+r[1])/s,s/4,(r[7]+r[5])/s,(r[6]-r[2])/s];}
  else{const s=2*Math.sqrt(1+r[8]-r[0]-r[4]);q=[(r[6]+r[2])/s,(r[7]+r[5])/s,s/4,(r[1]-r[3])/s];}
  return {position:[m[12],m[13],m[14]],quaternion:normalizedQuaternion(q,`node ${index} matrix rotation`),scale};
}
export async function compileGltf(input, options={}) {
  const {json:gltf,bin}=await sourceDocument(input);
  if(!gltf?.asset||gltf.asset.version!=='2.0'||(gltf.asset.minVersion&&gltf.asset.minVersion!=='2.0'))fail('input must use glTF 2.0');
  checkExtensions(gltf);
  for(const key of ['buffers','bufferViews','accessors','meshes','nodes','materials','textures','images','samplers','skins','animations','scenes'])if(gltf[key]!==undefined&&!Array.isArray(gltf[key]))fail(`${key} must be an array`);
  const warnings=[], warn=message=>{if(!warnings.includes(message))warnings.push(message);};
  const resolve=async uri=>{if(typeof uri!=='string')fail('resource URI must be a string');if(uri.startsWith('data:'))return decodeDataUri(uri);if(!options.resolveUri)fail(`external resource needs resolveUri: ${uri}`);return bytesOf(await options.resolveUri(uri));};
  const buffers=await Promise.all((gltf.buffers||[]).map(async(b,i)=>{const data=b.uri!==undefined?await resolve(b.uri):i===0&&bin?bin:fail(`buffer ${i} has no URI or GLB BIN`);integer(b.byteLength,`buffer ${i} byteLength`);if(data.length<b.byteLength||(!b.uri&&data.length>b.byteLength+3))fail(`buffer ${i} byteLength mismatch`);return data.subarray(0,b.byteLength);}));
  const getView=index=>{
    integer(index,'bufferView index');const b=gltf.bufferViews?.[index];if(!b)fail(`bufferView ${index} is missing`);
    const data=buffers[integer(b.buffer,'buffer index')];if(!data)fail(`bufferView ${index} refers to a missing buffer`);
    const offset=b.byteOffset||0;range(offset,b.byteLength,data.length,`bufferView ${index}`);return {definition:b,bytes:data.subarray(offset,offset+b.byteLength)};
  };
  const accessor=(index,expected)=>{
    integer(index,'accessor index');const a=gltf.accessors?.[index];if(!a)fail(`accessor ${index} is missing`);if(a.sparse)fail(`accessor ${index}: sparse data is unsupported`);
    const components=({SCALAR:1,VEC2:2,VEC3:3,VEC4:4,MAT4:16})[a.type];if(!components||expected&&a.type!==expected)fail(`accessor ${index}: expected ${expected||'a supported type'}`);
    const formats={5120:[1,'getInt8'],5121:[1,'getUint8'],5122:[2,'getInt16'],5123:[2,'getUint16'],5125:[4,'getUint32'],5126:[4,'getFloat32']};const format=formats[a.componentType];if(!format)fail(`accessor ${index}: unsupported component type`);
    const count=integer(a.count,`accessor ${index} count`,LIMIT),{definition,bytes}=getView(a.bufferView),packed=components*format[0],stride=definition.byteStride||packed,offset=a.byteOffset||0;
    if(stride<packed||stride%format[0]||offset%format[0]||definition.byteStride&&(stride>252||stride%4))fail(`accessor ${index}: invalid stride/alignment`);
    range(offset,count?(count-1)*stride+packed:0,bytes.length,`accessor ${index}`);
    const dv=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),values=new Array(count*components);
    if(a.normalized&&[5125,5126].includes(a.componentType))fail(`accessor ${index}: invalid normalized component type`);
    for(let i=0;i<count;i++)for(let j=0;j<components;j++){
      let value=dv[format[1]](offset+i*stride+j*format[0],true);
      if(a.normalized)value=a.componentType===5120?Math.max(value/127,-1):a.componentType===5121?value/255:a.componentType===5122?Math.max(value/32767,-1):value/65535;
      values[i*components+j]=finite(value,`accessor ${index} value`);
    }
    return {values,count,components,componentType:a.componentType,normalized:!!a.normalized,type:a.type};
  };
  const model={vertices:[],skinVertices:[],indices:[],meshes:[],primitives:[],nodes:[],materials:[],textures:[],skins:[],animations:[],roots:[]};
  const imageCache=new Map();
  for(let i=0;i<(gltf.textures||[]).length;i++){
    const t=gltf.textures[i],source=integer(t.source,`texture ${i} source`),image=gltf.images?.[source];if(!image)fail(`texture ${i} source image is missing`);
    if(image.mimeType&&image.mimeType!=='image/png')fail(`texture ${i}: only PNG is supported`);
    if(!imageCache.has(source)){
      const data=image.uri!==undefined?await resolve(image.uri):image.bufferView!==undefined?getView(image.bufferView).bytes:fail(`image ${source} has no data`);
      imageCache.set(source,await decodePng(data));
    }
    const sampler=t.sampler===undefined?{}:gltf.samplers?.[integer(t.sampler,'sampler index')];if(!sampler)fail(`texture ${i} sampler is missing`);
    const wrap=value=>value===undefined||value===10497?1:value===33071?0:value===33648?2:fail(`texture ${i}: invalid wrap mode`);
    if(sampler.magFilter!==undefined&&![9728,9729].includes(sampler.magFilter))fail(`texture ${i}: invalid magnification filter`);
    if(sampler.minFilter!==undefined&&![9728,9729,9984,9985,9986,9987].includes(sampler.minFilter))fail(`texture ${i}: invalid minification filter`);
    if(sampler.minFilter>=9984)warn('Mipmapped texture samplers are reduced to nearest base-level sampling; mipmaps are not stored.');
    if((sampler.magFilter??9729)!==9728||(sampler.minFilter??9729)!==9728)warn('Texture filtering is compiled to nearest sampling; linear filtering is not implemented by this renderer.');
    model.textures.push({...imageCache.get(source),wrapS:wrap(sampler.wrapS),wrapT:wrap(sampler.wrapT),filter:0});
  }
  for(let i=0;i<(gltf.materials||[]).length;i++){
    const m=gltf.materials[i],pbr=m.pbrMetallicRoughness||{};
    if(m.normalTexture||m.occlusionTexture||m.emissiveTexture||pbr.metallicRoughnessTexture)fail(`material ${i}: normal/occlusion/emissive/metallic-roughness maps must be baked into base color`);
    if(m.emissiveFactor?.some(x=>x!==0))fail(`material ${i}: emissive factors are unsupported`);
    if(pbr.metallicFactor!==undefined||pbr.roughnessFactor!==undefined)warn('Metallic and roughness factors are approximated using base color and simple runtime lighting.');
    const color=array(pbr.baseColorFactor,4,`material ${i} baseColorFactor`,[1,1,1,1]);if(color.some(x=>x<0||x>1))fail(`material ${i}: base color components must be in [0,1]`);
    let texture=-1;if(pbr.baseColorTexture){if((pbr.baseColorTexture.texCoord||0)!==0)fail(`material ${i}: only UV0 is supported`);texture=integer(pbr.baseColorTexture.index,'texture index');if(!model.textures[texture])fail(`material ${i}: texture is missing`);}
    const alpha=m.alphaMode||'OPAQUE';if(!['OPAQUE','MASK','BLEND'].includes(alpha))fail(`material ${i}: invalid alpha mode`);
    const cutoff=finite(m.alphaCutoff??0.5,`material ${i} alpha cutoff`);if(cutoff<0||cutoff>1)fail(`material ${i}: alpha cutoff must be in [0,1]`);
    model.materials.push({baseColor:color,texture,flags:(m.doubleSided?1:0)|(alpha==='MASK'?2:alpha==='BLEND'?4:0),alphaCutoff:cutoff});
  }
  // Material -1 resolves to the runtime's default white material.
  for(let mi=0;mi<(gltf.meshes||[]).length;mi++){
    const mesh=gltf.meshes[mi];if(mesh.weights?.length)fail(`mesh ${mi}: morph weights are unsupported`);if(!Array.isArray(mesh.primitives)||!mesh.primitives.length)fail(`mesh ${mi}: primitives are missing`);
    const firstPrimitive=model.primitives.length;
    for(let pi=0;pi<mesh.primitives.length;pi++){
      const p=mesh.primitives[pi],label=`mesh ${mi} primitive ${pi}`;if((p.mode??4)!==4)fail(`${label}: only TRIANGLES are supported`);if(p.targets?.length)fail(`${label}: morph targets are unsupported`);
      const attrs=p.attributes;if(!attrs||attrs.POSITION===undefined)fail(`${label}: POSITION is missing`);
      for(const key of Object.keys(attrs))if(!['POSITION','NORMAL','TEXCOORD_0','COLOR_0','JOINTS_0','WEIGHTS_0'].includes(key))fail(`${label}: unsupported attribute ${key}`);
      const pos=accessor(attrs.POSITION,'VEC3');if(pos.componentType!==5126||pos.normalized||!pos.count)fail(`${label}: POSITION must be nonempty float VEC3`);
      const attr=(name,type)=>{if(attrs[name]===undefined)return null;const a=accessor(attrs[name],type);if(a.count!==pos.count)fail(`${label}: ${name} count differs from POSITION`);return a;};
      const normal=attr('NORMAL','VEC3'),uv=attr('TEXCOORD_0','VEC2'),color=attr('COLOR_0'),joints=attr('JOINTS_0','VEC4'),weights=attr('WEIGHTS_0','VEC4');
      if(normal&&(normal.componentType!==5126||normal.normalized))fail(`${label}: normals must be float VEC3`);
      if(uv&&!([5126].includes(uv.componentType)||[5121,5123].includes(uv.componentType)&&uv.normalized))fail(`${label}: invalid UV component format`);
      if(color&&(!['VEC3','VEC4'].includes(color.type)||!(color.componentType===5126||[5121,5123].includes(color.componentType)&&color.normalized)))fail(`${label}: invalid vertex color format`);
      if(!!joints!==!!weights)fail(`${label}: JOINTS_0 and WEIGHTS_0 must be provided together`);
      if(joints&&(![5121,5123].includes(joints.componentType)||joints.normalized))fail(`${label}: joints must use unsigned byte/short`);
      if(weights&&!(weights.componentType===5126||[5121,5123].includes(weights.componentType)&&weights.normalized))fail(`${label}: invalid skin weight format`);
      let vertices=[];
      for(let vi=0;vi<pos.count;vi++){
        let rgb=0xffff;if(color){const c=color.values.slice(vi*color.components,(vi+1)*color.components);if(c.some(x=>x<0||x>1)||color.components===4&&Math.abs(c[3]-1)>1e-6)fail(`${label}: vertex color alpha must be one and RGB must be in [0,1]`);rgb=rgb565(c);}
        vertices.push({position:pos.values.slice(vi*3,vi*3+3),normal:normal?normal.values.slice(vi*3,vi*3+3):[0,0,0],uv:uv?uv.values.slice(vi*2,vi*2+2):[0,0],color:rgb});
      }
      let indices;if(p.indices===undefined)indices=Array.from({length:pos.count},(_,i)=>i);else{const a=accessor(p.indices,'SCALAR');if(![5121,5123,5125].includes(a.componentType)||a.normalized)fail(`${label}: indices must be unsigned integers`);indices=a.values;}
      if(!indices.length||indices.length%3||indices.some(v=>v>=pos.count))fail(`${label}: invalid triangle indices`);
      const cookedVertexCount=normal?vertices.length:indices.length;
      if(cookedVertexCount>LIMIT-model.vertices.length||indices.length>LIMIT-model.indices.length)fail('geometry exceeds import count budget');
      // glTF without NORMAL uses face normals, so each triangle needs its own
      // vertices. Retain the source index mapping for UV/color and skin data.
      let vertexRemap=null;
      if(!normal){
        warn('missing normals flattened; may increase vertices');
        const sourceVertices=vertices;vertexRemap=indices;vertices=[];
        for(let i=0;i<vertexRemap.length;i+=3){
          const a=sourceVertices[vertexRemap[i]].position,b=sourceVertices[vertexRemap[i+1]].position,c=sourceVertices[vertexRemap[i+2]].position;
          const u=b.map((x,j)=>x-a[j]),v=c.map((x,j)=>x-a[j]),cross=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]],length=Math.hypot(...cross);
          const faceNormal=length>1e-20?cross.map(x=>x/length):[0,0,1];
          for(let j=0;j<3;j++){const source=sourceVertices[vertexRemap[i+j]];vertices.push({position:source.position.slice(),normal:faceNormal.slice(),uv:source.uv.slice(),color:source.color});}
        }
        indices=Array.from({length:vertices.length},(_,i)=>i);
      }
      const material=p.material===undefined?-1:integer(p.material,`${label} material`);if(material>=model.materials.length)fail(`${label}: material is missing`);
      const primitive={vertexStart:model.vertices.length,vertexCount:vertices.length,indexStart:model.indices.length,indexCount:indices.length,material,mesh:mi,skinStart:0xffffffff};
      if(joints){primitive.skinStart=model.skinVertices.length;for(let vi=0;vi<vertices.length;vi++){const source=vertexRemap?vertexRemap[vi]:vi,w=weights.values.slice(source*4,source*4+4),sum=w.reduce((a,b)=>a+b,0);if(w.some(x=>x<0)||sum<1e-12)fail(`${label}: skin weights must be nonnegative with nonzero sum`);model.skinVertices.push({joints:joints.values.slice(source*4,source*4+4),weights:w.map(x=>x/sum)});}}
      for(const vertex of vertices)model.vertices.push(vertex);for(const index of indices)model.indices.push(index);model.primitives.push(primitive);
      if(model.vertices.length>LIMIT||model.indices.length>LIMIT)fail('geometry exceeds import count budget');
    }
    model.meshes.push({firstPrimitive,primitiveCount:model.primitives.length-firstPrimitive});
  }
  const sourceNodes=gltf.nodes||[];
  for(let i=0;i<sourceNodes.length;i++){
    const n=sourceNodes[i];if(n.weights?.length)fail(`node ${i}: morph weights are unsupported`);if(n.camera!==undefined)warn('glTF cameras are not imported; use the runtime camera.');
    const mesh=n.mesh===undefined?-1:integer(n.mesh,`node ${i} mesh`),skin=n.skin===undefined?-1:integer(n.skin,`node ${i} skin`);
    if(mesh>=model.meshes.length)fail(`node ${i}: mesh is missing`);if(skin>=0&&mesh<0)fail(`node ${i}: skin requires a mesh`);
    model.nodes.push({parent:-1,mesh,skin,name:String(n.name??`node_${i}`),...decomposeNode(n,i)});
  }
  sourceNodes.forEach((n,i)=>{if(n.children!==undefined&&!Array.isArray(n.children))fail(`node ${i}: children must be an array`);for(const child of n.children||[]){integer(child,'child node index');if(child>=model.nodes.length||child===i||model.nodes[child].parent!==-1)fail(`node ${i}: invalid or multiply-parented child`);model.nodes[child].parent=i;}});
  checkHierarchy(model.nodes,'node hierarchy has a cycle');
  for(let i=0;i<(gltf.skins||[]).length;i++){
    const s=gltf.skins[i];if(!Array.isArray(s.joints)||!s.joints.length||s.joints.length>65536)fail(`skin ${i}: invalid joint array`);
    const seen=new Set(),joints=s.joints.map(j=>{integer(j,'skin joint');if(j>=model.nodes.length||seen.has(j))fail(`skin ${i}: invalid/duplicate joint node`);seen.add(j);return j;});
    let inverseBindMatrices;if(s.inverseBindMatrices===undefined)inverseBindMatrices=joints.map(identity);else{const a=accessor(s.inverseBindMatrices,'MAT4');if(a.componentType!==5126||a.count!==joints.length)fail(`skin ${i}: inverse-bind matrix format/count mismatch`);inverseBindMatrices=joints.map((_,j)=>a.values.slice(j*16,j*16+16));}
    inverseBindMatrices.forEach(m=>checkInverseBind(m,`skin ${i}`));
    const skeletonRoot=s.skeleton===undefined?-1:integer(s.skeleton,'skin skeleton root');if(skeletonRoot>=model.nodes.length)fail(`skin ${i}: skeleton root is missing`);
    model.skins.push({joints,inverseBindMatrices,skeletonRoot,name:String(s.name??`skin_${i}`)});
  }
  model.nodes.forEach((n,i)=>{if(n.skin<0)return;const s=model.skins[n.skin];if(!s)fail(`node ${i}: skin is missing`);const mesh=model.meshes[n.mesh];for(let p=mesh.firstPrimitive;p<mesh.firstPrimitive+mesh.primitiveCount;p++){const primitive=model.primitives[p];if(primitive.skinStart===0xffffffff)fail(`node ${i}: skinned mesh lacks joint attributes`);for(let v=primitive.skinStart;v<primitive.skinStart+primitive.vertexCount;v++)if(model.skinVertices[v].joints.some(j=>j>=s.joints.length))fail(`node ${i}: vertex joint index exceeds skin joint count`);}});
  for(let ai=0;ai<(gltf.animations||[]).length;ai++){
    const a=gltf.animations[ai];if(!Array.isArray(a.channels)||!Array.isArray(a.samplers)||!a.channels.length)fail(`animation ${ai}: channels/samplers are missing`);
    const channels=[],seen=new Set();let duration=0;
    for(const channel of a.channels){
      const node=integer(channel.target?.node,'animation target node'),path=channel.target?.path;if(node>=model.nodes.length||!['translation','rotation','scale'].includes(path))fail(`animation ${ai}: unsupported/missing node target ${path}`);
      const key=`${node}:${path}`;if(seen.has(key))fail(`animation ${ai}: duplicate channel ${key}`);seen.add(key);
      const s=a.samplers[integer(channel.sampler,'animation sampler index')];if(!s)fail(`animation ${ai}: sampler is missing`);const interpolation=s.interpolation||'LINEAR';if(!['LINEAR','STEP'].includes(interpolation))fail(`animation ${ai}: ${interpolation} interpolation is unsupported`);
      const times=accessor(s.input,'SCALAR'),values=accessor(s.output,path==='rotation'?'VEC4':'VEC3');if(times.componentType!==5126||values.componentType!==5126||!times.count||times.count!==values.count)fail(`animation ${ai}: invalid float keyframe format/count`);
      if(times.values.some((t,i)=>t<0||i>0&&t<=times.values[i-1]))fail(`animation ${ai}: key times must be nonnegative and increasing`);
      if(path==='rotation')for(let i=0;i<values.count;i++)values.values.splice(i*4,4,...normalizedQuaternion(values.values.slice(i*4,i*4+4),`animation ${ai} quaternion`));
      duration=Math.max(duration,times.values.at(-1));channels.push({node,path,interpolation,times:times.values,values:values.values});
    }
    model.animations.push({name:String(a.name??`animation_${ai}`),duration,channels});
  }
  if(gltf.scenes?.length){const sceneIndex=integer(gltf.scene??0,'default scene index'),scene=gltf.scenes[sceneIndex];if(!scene)fail('default scene is missing');if(scene.nodes!==undefined&&!Array.isArray(scene.nodes))fail('scene nodes must be an array');const seen=new Set();model.roots=(scene.nodes||[]).map(n=>{integer(n,'scene root');if(n>=model.nodes.length||model.nodes[n].parent!==-1||seen.has(n))fail('scene contains an invalid root node');seen.add(n);return n;});}
  else model.roots=model.nodes.flatMap((n,i)=>n.parent===-1?[i]:[]);
  const activeNode=index=>{let top=index;while(model.nodes[top].parent>=0)top=model.nodes[top].parent;return model.roots.includes(top);};
  model.nodes.forEach((n,i)=>{if(n.skin>=0&&activeNode(i)&&model.skins[n.skin].joints.some(j=>!activeNode(j)))fail(`node ${i}: active skinned meshes require every joint to belong to the active scene`);});
  const binary=encodeNg3d(model);
  return {binary,model,clips:model.animations.map(a=>({name:a.name,duration:a.duration})),warnings};
}

class Writer {
  constructor(){this.bytes=new Uint8Array(1024);this.length=0;this.view=new DataView(this.bytes.buffer);}
  allocate(n){integer(n,'payload size');const at=(this.length+3)&~3,end=at+n;if(end>0x7fffffff)fail('asset exceeds 2 GiB');if(end>this.bytes.length){let size=this.bytes.length;while(size<end)size=Math.min(0x7fffffff,size*2);const b=new Uint8Array(size);b.set(this.bytes);this.bytes=b;this.view=new DataView(b.buffer);}this.length=end;return at;}
  u32(at,n){this.view.setUint32(at,n,true);} i32(at,n){this.view.setInt32(at,n,true);} f32(at,n){this.view.setFloat32(at,finite(n,'binary float'),true);}
  floats(values){const at=this.allocate(values.length*4);values.forEach((x,i)=>this.f32(at+i*4,x));return at;}
  name(value){if(value.includes('\0'))fail('asset names cannot contain NUL');const b=encoder.encode(value),at=this.allocate(b.length+1);this.bytes.set(b,at);return[at,b.length];}
}
function encodeNg3d(model){
  const w=new Writer();w.allocate(32+SECTIONS.length*16);w.bytes.set(encoder.encode('NG3D'));w.view.setUint16(4,1,true);w.view.setUint16(6,32,true);w.u32(12,SECTIONS.length);w.u32(16,32);
  const channels=model.animations.flatMap(a=>a.channels),rows=[model.vertices,model.skinVertices,model.indices,model.meshes,model.primitives,model.nodes,model.materials,model.textures,model.skins,model.animations,channels,model.roots],sections={};
  SECTIONS.forEach(([tag,stride],i)=>{const at=w.allocate(rows[i].length*stride),entry=32+i*16;w.bytes.set(encoder.encode(tag),entry);w.u32(entry+4,rows[i].length);w.u32(entry+8,stride);w.u32(entry+12,at);sections[tag]=at;});
  model.vertices.forEach((v,i)=>{const at=sections.VERT+i*36;[...v.position,...v.normal,...v.uv].forEach((x,j)=>w.f32(at+j*4,x));w.view.setUint16(at+32,v.color,true);});
  model.skinVertices.forEach((v,i)=>{const at=sections.VSKN+i*24;v.joints.forEach((j,k)=>w.view.setUint16(at+k*2,j,true));v.weights.forEach((x,k)=>w.f32(at+8+k*4,x));});
  model.indices.forEach((x,i)=>w.u32(sections.INDX+i*4,x));
  model.meshes.forEach((m,i)=>{const at=sections.MESH+i*8;w.u32(at,m.firstPrimitive);w.u32(at+4,m.primitiveCount);});
  model.primitives.forEach((p,i)=>{const at=sections.PRIM+i*32;[p.vertexStart,p.vertexCount,p.indexStart,p.indexCount,p.material,p.mesh,p.skinStart,0].forEach((x,j)=>w.u32(at+j*4,x));});
  model.nodes.forEach((n,i)=>{const at=sections.NODE+i*60;w.i32(at,n.parent);w.i32(at+4,n.mesh);w.i32(at+8,n.skin);const[name,length]=w.name(n.name);w.u32(at+12,name);w.u32(at+16,length);[...n.position,...n.quaternion,...n.scale].forEach((x,j)=>w.f32(at+20+j*4,x));});
  model.materials.forEach((m,i)=>{const at=sections.MATL+i*28;m.baseColor.forEach((x,j)=>w.f32(at+j*4,x));w.i32(at+16,m.texture);w.u32(at+20,m.flags);w.f32(at+24,m.alphaCutoff);});
  model.textures.forEach((t,i)=>{const at=sections.TEXR+i*24,pixels=w.allocate(t.pixels.length*2);t.pixels.forEach((x,j)=>w.view.setUint16(pixels+j*2,x,true));let alpha=0;if(t.alpha){alpha=w.allocate(t.alpha.length);w.bytes.set(t.alpha,alpha);}w.u32(at,t.width);w.u32(at+4,t.height);w.u32(at+8,pixels);w.u32(at+12,alpha);w.u32(at+16,t.wrapS|(t.wrapT<<2)|(t.filter<<4));});
  model.skins.forEach((s,i)=>{const at=sections.SKIN+i*24,joints=w.allocate(s.joints.length*4);s.joints.forEach((x,j)=>w.u32(joints+j*4,x));w.u32(at,joints);w.u32(at+4,s.joints.length);w.u32(at+8,w.floats(s.inverseBindMatrices.flat()));w.i32(at+12,s.skeletonRoot);const[name,length]=w.name(s.name);w.u32(at+16,name);w.u32(at+20,length);});
  let firstChannel=0;model.animations.forEach((a,i)=>{const at=sections.ANIM+i*16;w.u32(at,firstChannel);w.u32(at+4,a.channels.length);firstChannel+=a.channels.length;const[name,length]=w.name(a.name);w.u32(at+8,name);w.u32(at+12,length);});
  channels.forEach((c,i)=>{const at=sections.CHAN+i*24;w.u32(at,c.node);w.u32(at+4,['translation','rotation','scale'].indexOf(c.path));w.u32(at+8,c.interpolation==='STEP'?1:0);w.u32(at+12,c.times.length);w.u32(at+16,w.floats(c.times));w.u32(at+20,w.floats(c.values));});
  model.roots.forEach((x,i)=>w.u32(sections.ROOT+i*4,x));w.allocate(0);w.u32(8,w.length);return w.bytes.slice(0,w.length);
}

/** Decode the same cooked asset used by firmware, for the Studio preview. */
export function decodeNg3d(input){
  const b=bytesOf(input),v=new DataView(b.buffer,b.byteOffset,b.byteLength);if(b.length<32||String.fromCharCode(...b.subarray(0,4))!=='NG3D'||v.getUint16(4,true)!==1||v.getUint16(6,true)!==32||v.getUint32(8,true)!==b.length)fail('invalid NG3D header');
  const u=at=>{range(at,4,b.length,'NG3D field');return v.getUint32(at,true);},s=at=>{range(at,4,b.length,'NG3D signed field');return v.getInt32(at,true);},f=at=>{range(at,4,b.length,'NG3D float');return finite(v.getFloat32(at,true),'NG3D float');};
  if(u(20)||u(24)||u(28)||u(12)!==SECTIONS.length)fail('unsupported NG3D header fields');const dir=u(16);range(dir,u(12)*16,b.length,'NG3D directory');const sections={};
  for(let i=0;i<u(12);i++){const at=dir+i*16,tag=String.fromCharCode(...b.subarray(at,at+4)),spec=SECTIONS.find(x=>x[0]===tag);if(!spec||sections[tag]||u(at+8)!==spec[1])fail('invalid NG3D section');const count=u(at+4),offset=u(at+12);if(count>LIMIT||offset%4)fail('invalid NG3D section range');range(offset,count*spec[1],b.length,tag);sections[tag]={count,offset,stride:spec[1]};}
  const records=(tag,fn)=>{const section=sections[tag];return Array.from({length:section.count},(_,i)=>fn(section.offset+i*section.stride));};
  const floats=(at,n)=>{range(at,n*4,b.length,'float payload');return Array.from({length:n},(_,i)=>f(at+i*4));};
  const name=(at,n)=>{range(at,n+1,b.length,'name');if(b[at+n]||b.subarray(at,at+n).includes(0))fail('invalid NG3D name');return decoder.decode(b.subarray(at,at+n));};
  const model={
    vertices:records('VERT',at=>({position:floats(at,3),normal:floats(at+12,3),uv:floats(at+24,2),color:v.getUint16(at+32,true)})),
    skinVertices:records('VSKN',at=>({joints:Array.from({length:4},(_,i)=>v.getUint16(at+i*2,true)),weights:floats(at+8,4)})),
    indices:records('INDX',u),meshes:records('MESH',at=>({firstPrimitive:u(at),primitiveCount:u(at+4)})),
    primitives:records('PRIM',at=>({vertexStart:u(at),vertexCount:u(at+4),indexStart:u(at+8),indexCount:u(at+12),material:s(at+16),mesh:u(at+20),skinStart:u(at+24)})),
    nodes:records('NODE',at=>({parent:s(at),mesh:s(at+4),skin:s(at+8),name:name(u(at+12),u(at+16)),position:floats(at+20,3),quaternion:floats(at+32,4),scale:floats(at+48,3)})),
    materials:records('MATL',at=>({baseColor:floats(at,4),texture:s(at+16),flags:u(at+20),alphaCutoff:f(at+24)})),
    textures:records('TEXR',at=>{const width=u(at),height=u(at+4),offset=u(at+8),alpha=u(at+12),flags=u(at+16),count=width*height;if(!width||!height||count>LIMIT)fail('invalid NG3D texture size');range(offset,count*2,b.length,'texture pixels');if(alpha)range(alpha,count,b.length,'texture alpha');return{width,height,pixels:Uint16Array.from({length:count},(_,i)=>v.getUint16(offset+i*2,true)),alpha:alpha?b.slice(alpha,alpha+count):null,wrapS:flags&3,wrapT:(flags>>2)&3,filter:(flags>>4)&1};}),
    skins:records('SKIN',at=>{const count=u(at+4),offset=u(at);if(count>65536)fail('invalid NG3D joint count');range(offset,count*4,b.length,'joints');const matrices=floats(u(at+8),count*16);return{joints:Array.from({length:count},(_,i)=>u(offset+i*4)),inverseBindMatrices:Array.from({length:count},(_,i)=>matrices.slice(i*16,i*16+16)),skeletonRoot:s(at+12),name:name(u(at+16),u(at+20))};}),
    animations:[],roots:records('ROOT',u)
  };
  const channels=records('CHAN',at=>{const target=u(at+4),interpolation=u(at+8),count=u(at+12);if(target>2||interpolation>1||!count||count>LIMIT)fail('invalid NG3D animation channel');return{node:u(at),path:['translation','rotation','scale'][target],interpolation:interpolation?'STEP':'LINEAR',times:floats(u(at+16),count),values:floats(u(at+20),count*(target===1?4:3))};});
  model.animations=records('ANIM',at=>{const first=u(at),count=u(at+4);range(first,count,channels.length,'animation channels');const selected=channels.slice(first,first+count);return{name:name(u(at+8),u(at+12)),channels:selected,duration:selected.reduce((m,c)=>Math.max(m,c.times.at(-1)),0)};});
  // Cross references are checked here as well as by the firmware's stricter reader.
  model.primitives.forEach(p=>{range(p.vertexStart,p.vertexCount,model.vertices.length,'primitive vertices');range(p.indexStart,p.indexCount,model.indices.length,'primitive indices');if(p.indexCount%3||model.indices.slice(p.indexStart,p.indexStart+p.indexCount).some(i=>i>=p.vertexCount)||p.material < -1||p.material>=model.materials.length||p.mesh>=model.meshes.length)fail('invalid NG3D primitive');if(p.skinStart!==0xffffffff)range(p.skinStart,p.vertexCount,model.skinVertices.length,'primitive skin vertices');});
  model.meshes.forEach(m=>range(m.firstPrimitive,m.primitiveCount,model.primitives.length,'mesh primitives'));
  model.nodes.forEach((n,i)=>{if(n.parent < -1||n.parent>=model.nodes.length||n.parent===i||n.mesh < -1||n.mesh>=model.meshes.length||n.skin < -1||n.skin>=model.skins.length)fail('invalid NG3D node');});
  checkHierarchy(model.nodes,'NG3D node hierarchy has a cycle');
  model.roots.forEach(n=>{if(n>=model.nodes.length||model.nodes[n].parent!==-1)fail('invalid NG3D root');});
  channels.forEach(c=>{if(c.node>=model.nodes.length||c.times.some((t,i)=>t<0||i&&t<=c.times[i-1]))fail('invalid NG3D animation target/times');});
  return model;
}

export function cppHeader(binary,symbol='ng3d_model') {
  if(!/^[A-Za-z_][A-Za-z0-9_]*$/.test(symbol))fail('C++ symbol must be an identifier');
  const rows=[];for(let i=0;i<binary.length;i+=20)rows.push('  '+Array.from(binary.subarray(i,i+20),b=>'0x'+b.toString(16).padStart(2,'0')).join(', ')+',');
  return `#pragma once\n#include <cstddef>\n#include <cstdint>\nalignas(4) inline constexpr uint8_t ${symbol}[] = {\n${rows.join('\n')}\n};\ninline constexpr size_t ${symbol}_size = sizeof(${symbol});\n`;
}

async function cli(){
  const fsName='node:fs/promises',pathName='node:path';
  const [fs,path]=await Promise.all([import(/* @vite-ignore */ fsName),import(/* @vite-ignore */ pathName)]);
  const args=process.argv.slice(2);let input,output,header,symbol='ng3d_model';
  for(let i=0;i<args.length;i++){const a=args[i];if(a==='-o'||a==='--output')output=args[++i];else if(a==='--header')header=args[++i];else if(a==='--symbol')symbol=args[++i];else if(a==='-h'||a==='--help'){process.stdout.write('Usage: node compile-model.mjs input.glb -o output.ng3d [--header output.h] [--symbol model_name]\n');return;}else if(a.startsWith('-')||input)fail(`unknown argument ${a}`);else input=a;}
  if(!input)fail('input .gltf or .glb is required');output??=input.replace(/\.(?:gltf|glb)$/i,'')+'.ng3d';
  const inputPath=path.resolve(input),base=path.dirname(inputPath);
  const result=await compileGltf(new Uint8Array(await fs.readFile(inputPath)),{resolveUri:async uri=>{if(/^[A-Za-z][A-Za-z0-9+.-]*:/.test(uri)||uri.startsWith('//')||/[?#]/.test(uri))fail(`CLI accepts only relative local resource URIs: ${uri}`);let decoded;try{decoded=decodeURIComponent(uri);}catch{fail('invalid resource URI encoding');}const target=path.resolve(base,decoded);if(target!==base&&!target.startsWith(base+path.sep))fail('resource URI escapes the model directory');return new Uint8Array(await fs.readFile(target));}});
  await fs.writeFile(output,result.binary);if(header)await fs.writeFile(header,cppHeader(result.binary,symbol),'utf8');
  for(const warning of result.warnings)process.stderr.write(`Warning: ${warning}\n`);
  process.stdout.write(`${output}: ${result.binary.length} bytes, ${result.model.primitives.length} primitives, ${result.model.nodes.length} nodes, ${result.clips.length} clips\n`);
}
if(typeof process!=='undefined'&&process.versions?.node&&process.argv[1]){
  const mainPath=process.argv[1].replace(/\\/g,'/');
  if(decodeURIComponent(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/,'$1')===mainPath||import.meta.url===`file://${mainPath}`)cli().catch(error=>{process.stderr.write(`${error.message}\n`);process.exitCode=1;});
}
