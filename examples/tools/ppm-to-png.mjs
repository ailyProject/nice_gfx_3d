#!/usr/bin/env node
// Convert the native desktop renderer's P6 RGB output into a viewable PNG.
import { readFile,writeFile } from 'node:fs/promises';
import { deflateSync } from 'node:zlib';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const crcTable=Uint32Array.from({length:256},(_,n)=>{for(let i=0;i<8;i++)n=(n&1)?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
function crc32(bytes){let crc=0xffffffff;for(const value of bytes)crc=crcTable[(crc^value)&255]^(crc>>>8);return(crc^0xffffffff)>>>0;}
function chunk(name,data){const type=Buffer.from(name),size=Buffer.alloc(4),checksum=Buffer.alloc(4);size.writeUInt32BE(data.length);checksum.writeUInt32BE(crc32(Buffer.concat([type,data])));return Buffer.concat([size,type,data,checksum]);}
export async function ppmToPng(input,output){
  const bytes=await readFile(input),header=/^P6\s+(\d+)\s+(\d+)\s+255\n/.exec(bytes.subarray(0,128).toString('ascii'));
  if(!header)throw Error('Expected a P6 RGB8 PPM from render-model.');
  const width=Number(header[1]),height=Number(header[2]),offset=Buffer.byteLength(header[0]);
  if(!width||!height||width>4096||height>4096||bytes.length-offset!==width*height*3)throw Error('Invalid PPM dimensions or byte count.');
  const ihdr=Buffer.alloc(13);ihdr.writeUInt32BE(width);ihdr.writeUInt32BE(height,4);ihdr[8]=8;ihdr[9]=2;
  const rows=Buffer.alloc(height*(width*3+1));
  for(let y=0;y<height;y++)bytes.copy(rows,y*(width*3+1)+1,offset+y*width*3,offset+(y+1)*width*3);
  await writeFile(output,Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]));
  return{width,height,output};
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  if(process.argv.length!==4){process.stderr.write('Usage: node ppm-to-png.mjs input.ppm output.png\n');process.exitCode=2;}
  else ppmToPng(process.argv[2],process.argv[3]).then(value=>process.stdout.write(JSON.stringify(value)+'\n')).catch(error=>{process.stderr.write(error.message+'\n');process.exitCode=1;});
}
