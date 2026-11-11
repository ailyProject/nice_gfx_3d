// Offline topology hints. Nothing is removed from the model or its index order.
export function closedSurfaceHints(model,{opaqueOnly=false}={}){
  return model.primitives.map(primitive=>{
    const count=primitive.indexCount/3,map=[],welded=new Map(),edges=new Map();
    if(opaqueOnly){
      const material=model.materials[primitive.material];
      const texture=material&&material.texture>=0?model.textures[material.texture]:null;
      if(!material||(material.flags&6)||material.baseColor[3]<1||(texture?.alpha&&texture.alpha.some(value=>value!==255)))
        return {vertexCount:primitive.vertexCount,triangleCount:count,components:[],triangleComponents:[],eligibleTriangles:0};
    }
    for(let i=0;i<primitive.vertexCount;++i){
      const vertex=model.vertices[primitive.vertexStart+i];
      const skin=primitive.skinStart===0xffffffff?null:model.skinVertices[primitive.skinStart+i];
      const bits=values=>{const bytes=new Uint8Array(new Float32Array(values).buffer);let key='';for(const byte of bytes)key+=byte.toString(16).padStart(2,'0');return key;};
      const key=bits(vertex.position)+'|'+(skin?skin.joints.join(',')+'|'+bits(skin.weights):'');
      if(!welded.has(key))welded.set(key,i);map.push(welded.get(key));
    }
    const parents=Array.from({length:count},(_,i)=>i),invalid=new Uint8Array(count);
    const root=value=>{while(parents[value]!==value){parents[value]=parents[parents[value]];value=parents[value];}return value;};
    const triangles=Array.from({length:count},(_,i)=>model.indices.slice(primitive.indexStart+i*3,primitive.indexStart+i*3+3));
    for(let i=0;i<count;++i)for(let c=0;c<3;++c){
      const a=map[triangles[i][c]],b=map[triangles[i][(c+1)%3]],key=Math.min(a,b)+','+Math.max(a,b);
      if(a===b)invalid[i]=1;
      if(!edges.has(key))edges.set(key,[]);edges.get(key).push({triangle:i,direction:a<b?1:-1});
    }
    for(const uses of edges.values()){
      for(let i=1;i<uses.length;++i)parents[root(uses[i].triangle)]=root(uses[0].triangle);
      if(uses.length!==2||uses[0].direction===uses[1].direction)for(const use of uses)invalid[use.triangle]=1;
    }
    const groups=new Map();for(let i=0;i<count;++i){const r=root(i);if(!groups.has(r))groups.set(r,[]);groups.get(r).push(i);}
    const subtract=(a,b)=>a.map((value,i)=>value-b[i]);
    const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
    const dot=(a,b)=>a.reduce((sum,value,i)=>sum+value*b[i],0);
    const components=[],triangleComponents=Array(count).fill(65535);
    for(const group of groups.values()){
      if(group.some(i=>invalid[i]))continue;
      const vertices=[...new Set(group.flatMap(i=>triangles[i]))];
      const low=[Infinity,Infinity,Infinity],high=[-Infinity,-Infinity,-Infinity];
      let joint=0xffffffff,rigid=true;
      for(const i of vertices){
        const v=model.vertices[primitive.vertexStart+i];
        for(let c=0;c<3;++c){low[c]=Math.min(low[c],v.position[c]);high[c]=Math.max(high[c],v.position[c]);}
        if(primitive.skinStart!==0xffffffff){
          const skin=model.skinVertices[primitive.skinStart+i];
          if(skin.weights[0]!==1||skin.weights.slice(1).some(value=>value!==0))rigid=false;
          if(joint===0xffffffff)joint=skin.joints[0];else if(joint!==skin.joints[0])rigid=false;
        }
      }
      if(!rigid)continue;
      const center=low.map((value,i)=>Math.fround((value+high[i])*.5));
      const roundUp=value=>{const rounded=Math.fround(value);if(rounded>=value)return rounded;const bits=new Uint32Array(new Float32Array([rounded]).buffer);bits[0]++;return new Float32Array(bits.buffer)[0];};
      const halfExtent=low.map((value,i)=>roundUp(Math.max(Math.abs(value-center[i]),Math.abs(high[i]-center[i]))));
      let volume=0,absolute=0,degenerate=false;
      for(const i of group){
        const [a,b,c]=triangles[i].map(index=>model.vertices[primitive.vertexStart+index].position);
        const normal=cross(subtract(b,a),subtract(c,a));if(dot(normal,normal)===0)degenerate=true;
        const term=dot(subtract(a,center),cross(subtract(b,center),subtract(c,center)))/6;
        volume+=term;absolute+=Math.abs(term);
      }
      if(degenerate||!Number.isFinite(volume)||Math.abs(volume)<=absolute*1e-12||components.length>=128)continue;
      const component=components.length;components.push({center,halfExtent,joint,winding:volume>0?1:-1,triangles:group.length});
      for(const i of group)triangleComponents[i]=component;
    }
    return {vertexCount:primitive.vertexCount,triangleCount:count,components,triangleComponents,eligibleTriangles:triangleComponents.filter(value=>value!==65535).length};
  });
}

export function cppClosedSurfaceHints(hints,{namespace='nice_gfx_3d_hints',include=true}={}){
  if(!/^[A-Za-z_]\w*$/.test(namespace))throw Error('Invalid C++ hint namespace');
  const number=value=>{const text=Math.fround(value).toPrecision(9);return /[.eE]/.test(text)?text+'f':text+'.0f';};
  let source=(include?'#if defined(ARDUINO)\n#include "src/nice_gfx_3d/NICE_GFX_3D.h"\n#else\n#include <NICE_GFX_3D.h>\n#endif\n':'')+'namespace '+namespace+' {\n';
  for(const [i,hint] of hints.entries())if(hint.components.length){
    source+=`inline constexpr nice_gfx_3d::ClosedSurfaceComponent components${i}[]={\n`;
    for(const c of hint.components)source+=`  {{${c.center.map(number)}},{${c.halfExtent.map(number)}},${c.joint}u,${c.winding}},\n`;
    source+='};\n';
    source+=`inline constexpr uint16_t triangles${i}[]={\n`;
    for(let offset=0;offset<hint.triangleComponents.length;offset+=32)source+='  '+hint.triangleComponents.slice(offset,offset+32).join(',')+',\n';
    source+=`};\ninline constexpr nice_gfx_3d::ClosedSurfaceHints hint${i}={${hint.vertexCount},${hint.triangleCount},${hint.components.length},components${i},triangles${i}};\n`;
  }
  source+='inline nice_gfx_3d::Status attach(nice_gfx_3d::Model& model){\n';
  for(const [i,hint] of hints.entries())if(hint.components.length)source+=`  {const auto result=model.setClosedSurfaceHints(${i},&hint${i});if(result!=nice_gfx_3d::Status::Ok)return result;}\n`;
  return source+'  return nice_gfx_3d::Status::Ok;\n}\n}\n';
}
