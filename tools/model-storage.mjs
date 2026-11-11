// Source storage only. Runtime geometry, textures and animation bytes are exact.
// Compression and SHA-256 are injected so this module works in browsers and
// Node without filesystem, Buffer, native addons or a dependency on Studio.
export const ModelAssetCodec=Object.freeze({Raw:0,Zlib:1,ChannelDeltaBytePlanesZlib:2});
const maximumBytes=8*1024*1024;
const fail=message=>{throw Error('NG3D storage: '+message);};
const bytesOf=input=>input instanceof Uint8Array?input:fail('expected Uint8Array');
function channels(bytes) {
  if(bytes.length<32||String.fromCharCode(...bytes.subarray(0,4))!=='NG3D')fail('invalid header');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),u=at=>view.getUint32(at,true);
  if(view.getUint16(4,true)!==1||view.getUint16(6,true)!==32||u(8)!==bytes.length||u(12)!==12||u(20)||u(24)||u(28))fail('unsupported format');
  const directory=u(16),span=(at,length)=>at<=bytes.length&&length<=bytes.length-at;
  if(directory<32||directory%4||!span(directory,192))fail('invalid directory');
  const strides={VERT:36,VSKN:24,INDX:4,MESH:8,PRIM:32,NODE:60,MATL:28,TEXR:24,SKIN:24,ANIM:16,CHAN:24,ROOT:4},seen=new Set(),sections=[];
  let fixedEnd=directory+192,offset=0,count=0,found=false;
  for(let i=0;i<12;i++) {
    const at=directory+i*16,n=u(at+4),stride=u(at+8),start=u(at+12);
    const tag=String.fromCharCode(...bytes.subarray(at,at+4));
    if(!Object.hasOwn(strides,tag)||seen.has(tag)||stride!==strides[tag]||start<directory+192||start%4||!span(start,n*stride))fail('invalid section');
    seen.add(tag);sections.push({start,end:start+n*stride,count:n});
    fixedEnd=Math.max(fixedEnd,start+n*stride);
    if(tag==='CHAN') {
      if(found||stride!==24)fail('invalid channels');
      found=true;offset=start;count=n;
    }
  }
  for(let i=0;i<sections.length;i++)if(sections[i].count)for(let j=0;j<i;j++)
    if(sections[j].count&&sections[i].start<sections[j].end&&sections[j].start<sections[i].end)fail('overlapping sections');
  if(!found)fail('missing channels');
  const result=[];
  for(let i=0;i<count;i++) {
    const at=offset+i*24,property=u(at+4),interpolation=u(at+8),keys=u(at+12),times=u(at+16),values=u(at+20),components=property===1?4:3;
    if(property>2||interpolation>1||!keys||times<fixedEnd||values<fixedEnd||times%4||values%4||!span(times,keys*4)||!span(values,keys*components*4))fail('invalid channel payload');
    result.push({offset:times,keys,components:1},{offset:values,keys,components});
  }
  // Aliasing is legal in a borrowed model but cannot be predicted twice with
  // different strides. Plain deflate remains available for such assets.
  const ranges=result.map(x=>[x.offset,x.offset+x.keys*x.components*4]).sort((a,b)=>a[0]-b[0]);
  for(let i=1;i<ranges.length;i++)if(ranges[i][0]<ranges[i-1][1])fail('overlapping channel payloads');
  return result;
}
export function predictModelChannels(input) {
  const raw=bytesOf(input),records=channels(raw),bytes=new Uint8Array(raw),view=new DataView(bytes.buffer);
  for(const {offset,keys,components} of records)for(let i=keys*components-1;i>=components;i--) {
    const delta=(view.getUint32(offset+i*4,true)-view.getUint32(offset+(i-components)*4,true))|0;
    view.setUint32(offset+i*4,((delta<<1)^(delta>>31))>>>0,true);
  }
  return bytes;
}
export function restoreModelChannels(input) {
  const bytes=bytesOf(input),records=channels(bytes),view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  for(const {offset,keys,components} of records)for(let i=components;i<keys*components;i++) {
    const encoded=view.getUint32(offset+i*4,true),delta=(encoded>>>1)^-(encoded&1);
    view.setUint32(offset+i*4,(view.getUint32(offset+(i-components)*4,true)+delta)>>>0,true);
  }
  return bytes;
}
export function shuffleModelBytePlanes(input,reverse=false) {
  const source=bytesOf(input),output=new Uint8Array(source.length),words=Math.floor(source.length/4);
  for(let plane=0;plane<4;plane++)for(let word=0;word<words;word++) {
    const a=plane*words+word,b=word*4+plane;
    output[reverse?b:a]=source[reverse?a:b];
  }
  output.set(source.subarray(words*4),words*4);
  return output;
}
export function packModelBytes(input,{compress,digest}={}) {
  const raw=bytesOf(input);
  if(!raw.length||raw.length>maximumBytes)fail('raw asset exceeds 8 MiB');
  if(typeof compress!=='function')fail('provide synchronous zlib compression');
  if(!(digest instanceof Uint8Array)||digest.length!==32)fail('provide the 32-byte SHA-256 of the raw asset');
  let bytes=raw,codec=ModelAssetCodec.Raw;
  const consider=(candidate,value)=>{candidate=bytesOf(candidate);if(candidate.length<bytes.length){bytes=candidate;codec=value;}};
  consider(compress(raw),ModelAssetCodec.Zlib);
  try {consider(compress(shuffleModelBytePlanes(predictModelChannels(raw))),ModelAssetCodec.ChannelDeltaBytePlanesZlib);}catch(error) {
    if(!String(error?.message).startsWith('NG3D storage:'))throw error;
  }
  return {bytes,codec,rawBytes:raw.length,packedBytes:bytes.length,digest:new Uint8Array(digest)};
}
