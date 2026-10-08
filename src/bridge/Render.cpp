#include "../dllmain.h"
#include "State.h"
#include "Projection.h"
#include "imgui.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
namespace re4craft {
namespace {
using Microsoft::WRL::ComPtr;
struct Mesh {std::vector<P::RenVertex> vertices;std::vector<P::RenBatch> batches;};
std::map<std::tuple<int,int,int>,Mesh> sections;
std::map<unsigned,ComPtr<IDirect3DTexture9>> textures;
Mesh avatar,scene;
Mesh entitiesMesh;
P::WorldEntities entities{};
std::map<std::tuple<int,int,int>,std::vector<P::RenLight>> blockLights;
using LightKey=std::tuple<int,int,int>;
std::map<LightKey,cLight*> placedNativeLights;
cLight* nativeLightPool=nullptr;
using MakeLightFn=cLight*(__thiscall*)(cLightMgr*,const void*);
MakeLightFn makeLight=nullptr;
bool ownLight(cLight* l){return LightMgr&&nativeLightPool==LightMgr->m_Array_4&&LightMgr->indexOf(l)>=0&&(l->be_flag_4&0x201)==1&&l->Kind_29==0xe4;}
void dropNativeLights(){
 for(auto& [key,l]:placedNativeLights)if(ownLight(l))LightMgr->destroy(l);
 placedNativeLights.clear();nativeLightPool=nullptr;
}
P::RenScene sceneHeader{};
ComPtr<IDirect3DTexture9> overlay;
unsigned overlayW=0,overlayH=0;
bool flip=false;
uint64_t overlayFrame=0;
unsigned drawnTriangles=0;
ULONGLONG lastRenderLog=0;
Projection worldProjection;
D3DVIEWPORT9 worldViewport{};
bool worldActive=false,worldDrawn=false;
unsigned nativeWorldDraws=0;
DWORD worldDepthFunc=D3DCMP_LESSEQUAL;
const uint32_t* gxType=nullptr;
const float* gxProjection=nullptr;
const float* gxCorrection=nullptr;
struct CameraFrame { float view[12]{};Vec feet{};unsigned cameraMode=0; cLightEnv env{}; };
std::map<uintptr_t,CameraFrame> submittedCameras;
CameraFrame queuedCamera{},drawCamera{};
bool queuedCameraValid=false;
unsigned cameraSubmits=0,cameraMatches=0,missedWorldFrames=0;
using SetProjectionFn=void(__cdecl*)(const float*,unsigned);
SetProjectionFn enqueueProjection=nullptr,applyProjection=nullptr;
uintptr_t* gxQueue=nullptr;
// The game and GX worker run on different threads. Store the camera beside
// its exact queued projection command, then consume it on the GX worker.
void __cdecl queueProjectionHook(const float* matrix,unsigned type){
 CameraFrame frame;bool main=false;uintptr_t command=0;
 {
  std::lock_guard<std::recursive_mutex> lock(guard);auto g=GlobalPtr();
  main=g&&type==0&&matrix==&g->Camera_74.ProjMat_64[0][0];
  if(main){memcpy(frame.view,g->Camera_74.v_mat_30,sizeof(frame.view));if(PlayerPtr())frame.feet=PlayerPtr()->pos_94;frame.cameraMode=guest.cameraMode;if(LightMgr)frame.env=LightMgr->LightEnv_20;
   auto q=*gxQueue;auto head=*reinterpret_cast<uint32_t*>(q+0x300004);
   if(head+0x4c<=0x300000){command=q+head+8;submittedCameras[command]=frame;++cameraSubmits;}
  }
 }
 // A queue overflow can wait for the GPU; never hold the bridge mutex here.
 enqueueProjection(matrix,type);
 if(main&&!command){std::lock_guard<std::recursive_mutex> lock(guard);auto q=*gxQueue;auto head=*reinterpret_cast<uint32_t*>(q+0x300004);if(head>=0x4c){submittedCameras[q+head-0x4c+8]=frame;++cameraSubmits;}}
}
void __cdecl applyProjectionHook(const float* matrix,unsigned type){
 applyProjection(matrix,type);
 std::lock_guard<std::recursive_mutex> lock(guard);queuedCameraValid=false;
 if(type==0){auto i=submittedCameras.find(reinterpret_cast<uintptr_t>(matrix));if(i!=submittedCameras.end()){queuedCamera=i->second;queuedCameraValid=true;submittedCameras.erase(i);++cameraMatches;}}
}
bool locateGx(){
 static bool tried=false;
 if(tried)return gxType!=nullptr;tried=true;
 auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 // Offline-verified 1.1.0 EXE (the installer requires its original SHA256).
 // Fail closed on a different renderer instead of dereferencing guessed fields.
 const uint8_t expected[]={0x55,0x8b,0xec,0x8b,0x45,0x0c,0x85,0xc0};
 if(memcmp(reinterpret_cast<const void*>(base+0x532bd0),expected,sizeof(expected))){spd::log()->error("RE4CRAFT GX renderer signature mismatch; world drawing disabled");return false;}
 gxType=reinterpret_cast<const uint32_t*>(base+0xcc7270);
 gxProjection=reinterpret_cast<const float*>(base+0xcedaa0);
 gxCorrection=reinterpret_cast<const float*>(base+0xced730);
 return true;
}
struct Vertex {float x,y,z,rhw;D3DCOLOR color;float u,v;};
constexpr DWORD fvf=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1;
void upload(IDirect3DDevice9* d,unsigned id,unsigned w,unsigned h,const uint8_t* pixels){
 if(!w||!h||w>8192||h>8192||uint64_t(w)*h*4>64*1024*1024)return;
 ComPtr<IDirect3DTexture9> tex;if(FAILED(d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex,nullptr)))return;
 D3DLOCKED_RECT lock{};if(FAILED(tex->LockRect(0,&lock,nullptr,0)))return;
 for(unsigned y=0;y<h;y++){auto dst=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lock.pBits)+y*lock.Pitch);auto src=pixels+size_t(y)*w*4;
  for(unsigned x=0;x<w;x++)dst[x]=D3DCOLOR_ARGB(src[x*4+3],src[x*4],src[x*4+1],src[x*4+2]);}
 tex->UnlockRect(0);textures[id]=std::move(tex);
 spd::log()->info("RE4CRAFT texture id={} {}x{}",id,w,h);
}
bool readMesh(Mesh& out,const uint8_t* p,unsigned bytes,unsigned batches,unsigned vertices,unsigned header){
 if(vertices>1000000||batches>4096||uint64_t(header)+uint64_t(batches)*sizeof(P::RenBatch)+uint64_t(vertices)*sizeof(P::RenVertex)>bytes)return false;
 auto b=reinterpret_cast<const P::RenBatch*>(p+header);auto v=reinterpret_cast<const P::RenVertex*>(p+header+batches*sizeof(P::RenBatch));
 for(unsigned i=0;i<batches;i++)if(b[i].first>vertices||b[i].count>vertices-b[i].first||b[i].count%3)return false;
 out.batches.assign(b,b+batches);out.vertices.assign(v,v+vertices);return true;
}
void receive(IDirect3DDevice9* d,unsigned type,const uint8_t* p,unsigned bytes){
 switch(type){
 case P::kRenClearAll:sections.clear();solids.sections.clear();blockLights.clear();avatar={};scene={};break;
 case P::kRenSolids:if(bytes>=sizeof(P::RenSolids)){
  auto h=reinterpret_cast<const P::RenSolids*>(p);auto key=std::make_tuple(h->sx,h->sy,h->sz);
  if(!h->count)solids.sections.erase(key);
  else if(h->count<=4096&&bytes>=sizeof(*h)+512)memcpy(solids.sections[key].data(),p+sizeof(*h),512);
 }break;
 case P::kRenLights:if(bytes>=sizeof(P::RenLights)){
  auto h=reinterpret_cast<const P::RenLights*>(p);auto key=std::make_tuple(h->sx,h->sy,h->sz);
  if(!h->count)blockLights.erase(key);
  else if(h->count<=4096&&uint64_t(sizeof(*h))+h->count*sizeof(P::RenLight)<=bytes){auto lights=reinterpret_cast<const P::RenLight*>(p+sizeof(*h));blockLights[key].assign(lights,lights+h->count);}
 }break;
 case P::kRenAtlas:if(bytes>=sizeof(P::RenAtlas)){auto h=reinterpret_cast<const P::RenAtlas*>(p);if(uint64_t(h->width)*h->height*4+sizeof(*h)<=bytes)upload(d,0,h->width,h->height,p+sizeof(*h));}break;
 case P::kRenTexture:if(bytes>=sizeof(P::RenTexture)){auto h=reinterpret_cast<const P::RenTexture*>(p);if(h->id&&uint64_t(h->width)*h->height*4+sizeof(*h)<=bytes)upload(d,h->id,h->width,h->height,p+sizeof(*h));}break;
 case P::kRenAtlasRegion:if(bytes>=sizeof(P::RenAtlasRegion)&&textures.count(0)){
  auto h=reinterpret_cast<const P::RenAtlasRegion*>(p);D3DSURFACE_DESC desc{};textures[0]->GetLevelDesc(0,&desc);
  if(h->x<=desc.Width&&h->width<=desc.Width-h->x&&h->y<=desc.Height&&h->height<=desc.Height-h->y&&uint64_t(h->width)*h->height*4+sizeof(*h)<=bytes){
   RECT rect{LONG(h->x),LONG(h->y),LONG(h->x+h->width),LONG(h->y+h->height)};D3DLOCKED_RECT lock{};
   if(SUCCEEDED(textures[0]->LockRect(0,&lock,&rect,0))){auto src=p+sizeof(*h);
    for(unsigned y=0;y<h->height;y++){auto dst=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lock.pBits)+y*lock.Pitch);for(unsigned x=0;x<h->width;x++){auto s=src+(size_t(y)*h->width+x)*4;dst[x]=D3DCOLOR_ARGB(s[3],s[0],s[1],s[2]);}}textures[0]->UnlockRect(0);
   }
  }
 }break;
 case P::kRenSection:if(bytes>=sizeof(P::RenSection)){
  auto h=reinterpret_cast<const P::RenSection*>(p);auto key=std::make_tuple(h->sx,h->sy,h->sz);
  if(!h->vertexCount){sections.erase(key);break;}
  if(h->vertexCount>1000000||h->vertexCount%3||uint64_t(h->vertexCount)*sizeof(P::RenVertex)+sizeof(*h)>bytes)break;
  auto v=reinterpret_cast<const P::RenVertex*>(p+sizeof(*h));auto& mesh=sections[key];mesh.vertices.assign(v,v+h->vertexCount);
  // Each face may be translucent or cut out. Preserve Minecraft's vertex flags.
  mesh.batches.clear();unsigned start=0,flags=v[0].flags&3;
  for(unsigned i=3;i<h->vertexCount;i+=3)if((v[i].flags&3)!=flags){mesh.batches.push_back({0,start,i-start,flags});start=i;flags=v[i].flags&3;}
  mesh.batches.push_back({0,start,h->vertexCount-start,flags});
 }break;
 case P::kRenAvatar:if(bytes>=sizeof(P::RenAvatar)){auto h=reinterpret_cast<const P::RenAvatar*>(p);readMesh(avatar,p,bytes,h->batchCount,h->vertexCount,sizeof(*h));}break;
 case P::kRenScene:if(bytes>=sizeof(P::RenScene)){auto h=reinterpret_cast<const P::RenScene*>(p);if(readMesh(scene,p,bytes,h->batchCount,h->vertexCount,sizeof(*h)))sceneHeader=*h;}break;
 default:break;
 }
}
bool project(Vec world,Vertex& out){
 return worldProjection.project(world.x,world.y,world.z,out.x,out.y,out.z,out.rhw);
}
void setup(IDirect3DDevice9* d){
 d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(fvf);
 d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
 d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
 d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
 d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
 d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
 d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
 d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
 d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);d->SetTextureStageState(0,D3DTSS_RESULTARG,D3DTA_CURRENT);
 d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);d->SetRenderState(D3DRS_WRAP0,0);
 d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
}
float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec unit(Vec a){float len=std::sqrt(dot(a,a));return len>1e-6f?a/len:Vec{0,1,0};}
struct NativeLight { Vec position;float radius,intensity;GXColor color; };
std::vector<NativeLight> nativeLights;
struct McLight {Vec position;float radius;uint32_t color;};
std::vector<McLight> minecraftLights;
void gatherLights(){
 nativeLights.clear();minecraftLights.clear();
 // enable_C is temporary per-object state, cleared by the native renderer.
 if(LightMgr&&LightMgr->m_nArray_8<4096)for(auto& l:*LightMgr)if((l.be_flag_4&0x201)==1&&l.Kind_29!=0xe4&&l.Type_D!=5&&l.Radius_1C>0&&l.Intensity_24>0&&nativeLights.size()<128)nativeLights.push_back({l.World_144,l.Radius_1C,l.Intensity_24,l.Col_20});
 for(auto& [key,lights]:blockLights){auto [x,y,z]=key;for(auto l:lights){auto p=skycraft::McToSky(x*16+l.x+.5,y*16+l.y+.5,z*16+l.z+.5);if(dot(p-drawCamera.feet,p-drawCamera.feet)<48000.f*48000.f&&minecraftLights.size()<128)minecraftLights.push_back({p,l.level*1000.f,l.color});}}
}
void shade(Vec world,Vec normal,const P::RenVertex& v,float* color){
 const auto& env=drawCamera.env;auto amb=env.AmbientEm_FC;
 float face=.26f+.25f*std::max(0.f,normal.y)+.13f*std::abs(normal.x)+.08f*std::abs(normal.z);
 float light[3]={std::max(.13f,amb.r/255.f)+face,std::max(.13f,amb.g/255.f)+face,std::max(.13f,amb.b/255.f)+face};
 for(auto& l:nativeLights){Vec dir=l.position-world;float distance=std::sqrt(dot(dir,dir));if(distance>=l.radius)continue;float factor=std::clamp(l.intensity,0.f,4.f)*(1-distance/l.radius)*(.25f+.75f*std::max(0.f,dot(normal,unit(dir))));light[0]+=l.color.r/255.f*factor;light[1]+=l.color.g/255.f*factor;light[2]+=l.color.b/255.f*factor;}
 for(auto& l:minecraftLights){Vec dir=l.position-world;float distance=std::sqrt(dot(dir,dir));if(distance>=l.radius)continue;float f=(1-distance/l.radius)*(.25f+.75f*std::max(0.f,dot(normal,unit(dir))));for(int k=0;k<3;k++)light[k]+=((l.color>>(k*8))&255)/255.f*f;}
 float distance=-(worldProjection.view[8]*world.x+worldProjection.view[9]*world.y+worldProjection.view[10]*world.z+worldProjection.view[11]);
 float fog=0;if(static_cast<unsigned>(env.Fog_8.Type_0)!=0&&env.Fog_8.End_8>env.Fog_8.Start_4)fog=std::clamp((distance-env.Fog_8.Start_4)/(env.Fog_8.End_8-env.Fog_8.Start_4),0.f,1.f);
 const float fogColor[]={float(env.Fog_8.Color_C.r),float(env.Fog_8.Color_C.g),float(env.Fog_8.Color_C.b)};
 for(int k=0;k<3;k++){float tint=float((v.color>>(k*8))&255);color[k]=tint*std::clamp(light[k],.08f,1.f)*(1-fog)+fogColor[k]*fog;}
 color[3]=float(v.color>>24);
}
void appendTriangle(const ClipVertex* triangle,std::vector<Vertex>& output){
 ClipVertex clipped[12];int n=clipTriangle(triangle,clipped);
 for(int i=1;i<n-1;i++)for(int index:{0,i,i+1}){
  const auto& v=clipped[index];float inv=1/v.p[3];if(!std::isfinite(inv))return;
  Vertex q{worldProjection.x+(v.p[0]*inv*.5f+.5f)*worldProjection.width-.5f,worldProjection.y+(.5f-v.p[1]*inv*.5f)*worldProjection.height-.5f,
   worldProjection.minZ+v.p[2]*inv*(worldProjection.maxZ-worldProjection.minZ),inv,
   D3DCOLOR_ARGB(unsigned(std::clamp(v.c[3],0.f,255.f)),unsigned(std::clamp(v.c[0],0.f,255.f)),unsigned(std::clamp(v.c[1],0.f,255.f)),unsigned(std::clamp(v.c[2],0.f,255.f))),v.u,v.v};output.push_back(q);
 }
}
void drawMesh(IDirect3DDevice9* d,const Mesh& mesh,Vec origin,bool section){
 d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);d->SetRenderState(D3DRS_ZFUNC,worldDepthFunc);
 for(auto& batch:mesh.batches){
  auto tex=textures.find(batch.texture);if(tex==textures.end())continue;d->SetTexture(0,tex->second.Get());
  d->SetRenderState(D3DRS_ZWRITEENABLE,(section?(batch.flags&2):(batch.flags&1))?FALSE:TRUE);
  d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);d->SetRenderState(D3DRS_ALPHAREF,8);
  std::vector<Vertex> output;output.reserve(batch.count);
  for(unsigned i=batch.first;i<batch.first+batch.count;i+=3){ClipVertex triangle[3];Vec w[3];
   for(int k=0;k<3;k++){auto& v=mesh.vertices[i+k];w[k]=origin+Vec{v.x*1000,v.y*1000,v.z*1000};}
   Vec normal=unit(cross(w[1]-w[0],w[2]-w[0]));
   const Vec normals[]={{0,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
   unsigned encoded=(mesh.vertices[i].flags>>4)&7;if(section&&encoded>0&&encoded<7)normal=normals[encoded];
   for(int k=0;k<3;k++){auto& v=mesh.vertices[i+k];worldProjection.clip(w[k].x,w[k].y,w[k].z,triangle[k].p);shade(w[k],normal,v,triangle[k].c);triangle[k].u=v.u;triangle[k].v=v.v;}
   appendTriangle(triangle,output);
  }
  if(!output.empty()){drawnTriangles+=unsigned(output.size()/3);d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,unsigned(output.size()/3),output.data(),sizeof(Vertex));}
 }
}
void quad(Mesh& mesh,const Vec* p,const float* uv,uint32_t color=0xffffffff){
 const int indices[]={0,1,2,0,2,3};const float u[]={uv[0],uv[2],uv[2],uv[0]},v[]={uv[1],uv[1],uv[3],uv[3]};
 for(int k:indices)mesh.vertices.push_back({p[k].x,p[k].y,p[k].z,u[k],v[k],color,0x0f0f,1});
}
void arrow(Mesh& mesh,Vec position,Vec dir,const float uv[3][4],bool trident){
 Vec side=unit(Vec{dir.z,0,-dir.x}),up=unit(cross(side,dir));Vec fins[]={(up+side)*.70710678f,(up-side)*.70710678f};
 auto at=[&](float along,Vec f,float a,Vec f2=Vec{},float b=0.f){return position+dir*along+f*a+f2*b;};
 const float k=.9f/16; // Minecraft size; keep arrows readable in RE4.
 for(auto f:fins){Vec q[4];if(trident){q[0]=at(0,f,.9f);q[1]=at(.9f,f,0);q[2]=at(0,f,-.9f);q[3]=at(-.9f,f,0);}else{q[0]=at(-12*k,f,-2*k);q[1]=at(4*k,f,-2*k);q[2]=at(4*k,f,2*k);q[3]=at(-12*k,f,2*k);}quad(mesh,q,uv[0]);}
 if(!trident){Vec q[]={at(-11*k,fins[0],-2*k,fins[1],-2*k),at(-11*k,fins[0],2*k,fins[1],-2*k),at(-11*k,fins[0],2*k,fins[1],2*k),at(-11*k,fins[0],-2*k,fins[1],2*k)};quad(mesh,q,uv[1]);}
}
struct Stuck {cEm* actor;unsigned guid;cParts* root;cCoord* bone;Vec position,direction;unsigned weapon;ULONGLONG time;};
std::deque<Stuck> stuck;
float arrowUvs[3][3][4]{};bool haveArrowUvs[3]{};
Vec toLocal(const Mtx& mat,Vec p,bool point){
 if(point)p-=Vec{mat[0][3],mat[1][3],mat[2][3]};
 Vec col[]={{mat[0][0],mat[1][0],mat[2][0]},{mat[0][1],mat[1][1],mat[2][1]},{mat[0][2],mat[1][2],mat[2][2]}};
 return {dot(p,col[0])/std::max(.0001f,dot(col[0],col[0])),dot(p,col[1])/std::max(.0001f,dot(col[1],col[1])),dot(p,col[2])/std::max(.0001f,dot(col[2],col[2]))};
}
Vec toWorld(const Mtx& m,Vec p,bool point){return {m[0][0]*p.x+m[0][1]*p.y+m[0][2]*p.z+(point?m[0][3]:0),m[1][0]*p.x+m[1][1]*p.y+m[1][2]*p.z+(point?m[1][3]:0),m[2][0]*p.x+m[2][1]*p.y+m[2][2]*p.z+(point?m[2][3]:0)};}
void buildEntities(){
 entitiesMesh={};if(!skycraft::Link::Get().ReadWorldEntities(entities))entities.count=0;
 for(unsigned i=0;i<std::min(entities.count,P::kMaxWorldEntities);i++){
  const auto& e=entities.entities[i];Vec p{e.x,e.y+512,e.z};if(!std::isfinite(p.x+p.y+p.z))continue;
  if(e.kind==P::kWeArrow||e.kind==P::kWeTrident){float y=e.yaw*.0174532925f,pi=e.pitch*.0174532925f;Vec dir{std::sin(y)*std::cos(pi),std::sin(pi),std::cos(y)*std::cos(pi)};
   if(e.kind==P::kWeArrow){memcpy(arrowUvs[0],e.uv,sizeof(e.uv));haveArrowUvs[0]=true;}
   arrow(entitiesMesh,p,dir,e.uv,e.kind==P::kWeTrident);
  }else if(e.kind==P::kWeItem){float y=e.yaw*.0174532925f,h=e.scale*.5f;Vec r{std::cos(y)*h,0,std::sin(y)*h};Vec q[]={p-r+Vec{0,h,0},p+r+Vec{0,h,0},p+r-Vec{0,h,0},p-r-Vec{0,h,0}};quad(entitiesMesh,q,e.uv[0]);}
 }
 for(auto i=stuck.begin();i!=stuck.end();){
  if(!i->actor->IsValid()||i->actor->guid_F8!=i->guid||i->actor->childParts_F4!=i->root||GetTickCount64()-i->time>60000){i=stuck.erase(i);continue;}
  if(haveArrowUvs[0]){Vec p=toWorld(i->bone->mat_C,i->position,true)/1000.f,dir=unit(toWorld(i->bone->mat_C,i->direction,false));arrow(entitiesMesh,p,dir,arrowUvs[0],false);}++i;
 }
 if(!entitiesMesh.vertices.empty())entitiesMesh.batches.push_back({0,0,unsigned(entitiesMesh.vertices.size()),0});
}
unsigned shadowTriangles=0;
void drawShadows(IDirect3DDevice9* d){
 shadowTriangles=0;std::vector<Vertex> output;auto center=skycraft::SkyToMc(drawCamera.feet);unsigned count=0;
 // Project the block silhouette onto RE4's collision surface. The same depth
 // buffer occludes the shadow; terrain samples keep it attached to slopes.
 solids.cells({center.x-24,center.y-8,center.z-24},{center.x+24,center.y+12,center.z+24},[&](int x,int y,int z){
  if(count>=96||solids.occupied(x,y+1,z))return;
  double dist=std::hypot(x+.5-center.x,z+.5-center.z);if(dist>24)return;
  float nativeY=0;if(!TerrainGround(float(x+.5),float(z+.5),float(y+1.02),nativeY))return;
  float h=std::max(.15f,float(y+1)-nativeY),dx=.35f*h,dz=.22f*h;
  Vec corners[]={{float(x)-.02f,float(y),float(z)-.02f},{float(x+1)+dx,float(y),float(z)-.02f},{float(x+1)+dx,float(y),float(z+1)+dz},{float(x)-.02f,float(y),float(z+1)+dz}};
  float alpha=56.f*std::clamp(float((24-dist)/8),0.f,1.f);ClipVertex verts[4];
  for(int j=0;j<4;j++){float ground=0;if(!TerrainGround(corners[j].x,corners[j].z,float(y+1.02),ground))return;auto p=skycraft::McToSky(corners[j].x,ground+.005,corners[j].z);worldProjection.clip(p.x,p.y,p.z,verts[j].p);verts[j].c[3]=alpha;}
  ClipVertex a[]={verts[0],verts[1],verts[2]},b[]={verts[0],verts[2],verts[3]};appendTriangle(a,output);appendTriangle(b,output);++count;
 });
 if(output.empty())return;
 d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);
 d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);d->SetRenderState(D3DRS_ZFUNC,worldDepthFunc);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
 shadowTriangles=unsigned(output.size()/3);d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,shadowTriangles,output.data(),sizeof(Vertex));
 d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);
}
void updateOverlay(IDirect3DDevice9* d){
 auto& link=skycraft::Link::Get();if(!link.AcquireOverlayFrame())return;auto h=link.FrontHeader();
 if(!h->width||!h->height||h->width>P::kMaxOverlayW||h->height>P::kMaxOverlayH)return;
 if(!overlay||overlayW!=h->width||overlayH!=h->height){overlay.Reset();overlayW=h->width;overlayH=h->height;
  if(FAILED(d->CreateTexture(overlayW,overlayH,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&overlay,nullptr)))return;
  spd::log()->info("RE4CRAFT overlay {}x{}",overlayW,overlayH);
 }
 D3DLOCKED_RECT lock{};if(FAILED(overlay->LockRect(0,&lock,nullptr,0)))return;
 const auto src=link.FrontPixels();
 for(unsigned y=0;y<overlayH;y++){auto dst=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lock.pBits)+y*lock.Pitch);auto s=src+size_t(y)*overlayW*4;
  for(unsigned x=0;x<overlayW;x++)dst[x]=D3DCOLOR_ARGB(s[x*4+3],s[x*4],s[x*4+1],s[x*4+2]);}
 overlay->UnlockRect(0);flip=(h->flags&1)!=0;overlayFrame=h->frameId;
}
void finishWorld(IDirect3DDevice9* d,const char* reason){
 if(!worldActive)return;worldActive=false;
 if(worldDrawn||!MinecraftVisible()||!skycraft::Link::Get().McAlive())return;
 worldDrawn=true;drawnTriangles=0;
 ComPtr<IDirect3DStateBlock9> saved;if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&saved)))return;saved->Capture();
 setup(d);d->SetViewport(&worldViewport);
 gatherLights();buildEntities();drawShadows(d);
 for(auto& [key,mesh]:sections){auto [x,y,z]=key;drawMesh(d,mesh,skycraft::McToSky(x*16.,y*16.,z*16.),true);}
 drawMesh(d,scene,skycraft::McToSky(sceneHeader.originX,sceneHeader.originY,sceneHeader.originZ),false);
 drawMesh(d,entitiesMesh,Vec{},false);
 if(drawCamera.cameraMode!=0)drawMesh(d,avatar,drawCamera.feet,false);
 static bool armorLogged=false;
 if(!armorLogged&&!avatar.vertices.empty())for(const auto& b:avatar.batches)if(b.texture==3&&b.count){
  float lo=1e20f,hi=-1e20f;for(unsigned i=b.first;i<b.first+b.count;i++){lo=std::min({lo,avatar.vertices[i].u,avatar.vertices[i].v});hi=std::max({hi,avatar.vertices[i].u,avatar.vertices[i].v});}
  spd::log()->info("RE4CRAFT armor texture={} vertices={} UVrange={:.4f}..{:.4f}",b.texture,b.count,lo,hi);armorLogged=true;break;
 }
 saved->Apply();
 if(GetTickCount64()-lastRenderLog>5000){lastRenderLog=GetTickCount64();
  ComPtr<IDirect3DSurface9> depth;auto hr=d->GetDepthStencilSurface(&depth);D3DSURFACE_DESC ds{};if(depth)depth->GetDesc(&ds);
  spd::log()->info("RE4CRAFT world render boundary={} nativeDraws={} sections={} sceneVertices={} triangles={} depthHr={} depth={}x{} vp={}x{} gxScale={:.4f},{:.4f} zFunc={}",reason,nativeWorldDraws,sections.size(),scene.vertices.size(),drawnTriangles,hr,ds.Width,ds.Height,worldViewport.Width,worldViewport.Height,worldProjection.correction[0],worldProjection.correction[5],worldDepthFunc);
  spd::log()->info("RE4CRAFT renderer camera={}/{} missed={} solidsSections={} entityVertices={} stuck={} shadowTriangles={} nativeLights={} blockLights={} linkedLights={}",cameraMatches,cameraSubmits,missedWorldFrames,solids.sections.size(),entitiesMesh.vertices.size(),stuck.size(),shadowTriangles,nativeLights.size(),minecraftLights.size(),placedNativeLights.size());
 }
}
}
void RenderInit(){
 if(!locateGx())return;uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 auto enqueueThunk=base+0xdd05,applyThunk=base+0x16a9;
 auto jumpTo=[](uintptr_t p){return *reinterpret_cast<uint8_t*>(p)==0xe9?p+5+*reinterpret_cast<int32_t*>(p+1):uintptr_t(0);};
 if(jumpTo(enqueueThunk)!=base+0x557660||jumpTo(applyThunk)!=base+0x532bd0){spd::log()->error("RE4CRAFT camera queue thunk mismatch");return;}
 enqueueProjection=reinterpret_cast<SetProjectionFn>(base+0x557660);applyProjection=reinterpret_cast<SetProjectionFn>(base+0x532bd0);gxQueue=reinterpret_cast<uintptr_t*>(base+0xdd1a3c);
 InjectHook(enqueueThunk,queueProjectionHook,HookType::Jump);InjectHook(applyThunk,applyProjectionHook,HookType::Jump);
 spd::log()->info("RE4CRAFT queued-camera capture installed");
}
void RenderRoomReset(){dropNativeLights();stuck.clear();solids.sections.clear();blockLights.clear();sections.clear();avatar={};scene={};}
void SyncNativeLights(){
 if(!LightMgr||!PlayerPtr()||!owns||!skycraft::Link::Get().McAlive()){dropNativeLights();return;}
 if(nativeLightPool&&nativeLightPool!=LightMgr->m_Array_4)placedNativeLights.clear();
 nativeLightPool=LightMgr->m_Array_4;
 if(!makeLight){
  auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const uint8_t expected[]={0x55,0x8b,0xec,0x56,0x57,0x8b,0x7d,0x08,0x0f,0xb6,0x47,0x02};
  // Verified 1.1.0 cLightMgr::set(cLit*) factory: allocates from the manager,
  // constructs the light and links it into its alive list (650AD0 -> 64FDB0).
  if(memcmp(reinterpret_cast<const void*>(base+0x250ad0),expected,sizeof(expected)))return;
  makeLight=reinterpret_cast<MakeLightFn>(base+0x250ad0);
 }
 struct Candidate{LightKey key;P::RenLight light;float distance;};std::vector<Candidate> nearby;
 Vec feet=PlayerPtr()->pos_94;
 for(auto& [key,lights]:blockLights){auto [x,y,z]=key;for(auto l:lights){int bx=x*16+l.x,by=y*16+l.y,bz=z*16+l.z;Vec pos=skycraft::McToSky(bx+.5,by+.7,bz+.5),d=pos-feet;float dist=dot(d,d);if(l.level&&dist<32000.f*32000.f)nearby.push_back({{bx,by,bz},l,dist});}}
 std::sort(nearby.begin(),nearby.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});if(nearby.size()>12)nearby.resize(12);
 for(auto i=placedNativeLights.begin();i!=placedNativeLights.end();){bool keep=false;for(auto& c:nearby)if(c.key==i->first){keep=true;break;}if(!keep||!ownLight(i->second)){if(ownLight(i->second))LightMgr->destroy(i->second);i=placedNativeLights.erase(i);}else ++i;}
 for(auto& c:nearby){
  auto [x,y,z]=c.key;Vec pos=skycraft::McToSky(x+.5,y+.7,z+.5);auto l=c.light;
  float radius=l.level*600.f;float intensity=.8f;
  if((l.color>>24&15)==P::kLightFlame)intensity*=.93f+.07f*std::sin(float(GetTickCount64()%100000)*.013f+x*1.7f+z);
  GXColor color{uint8_t(l.color),uint8_t(l.color>>8),uint8_t(l.color>>16),128};
  auto i=placedNativeLights.find(c.key);cLight* native=i==placedNativeLights.end()?nullptr:i->second;
  if(!native){
   // cLit record layout verified at cLight::set (6509C0); type 2 is the
   // native point light used by the village's warm lamps. Id 0 is steady.
   uint8_t record[0x12c]{};record[0]=3;record[1]=2;record[3]=0x7f;record[0x1d]=0xe4;
   memcpy(record+4,&pos,sizeof(pos));memcpy(record+0x10,&radius,4);memcpy(record+0x14,&color,4);memcpy(record+0x18,&intensity,4);
   native=makeLight(LightMgr,record);if(!native)continue;placedNativeLights[c.key]=native;
  }
  native->Pos_10=native->World_144=pos;native->Radius_1C=radius;native->Intensity_24=intensity;native->Col_20=native->DispCol_13C=color;native->EnableMask_F=0x7f;
 }
}
void StickArrow(cEm* actor,const P::McEvent& e){
 if(!std::isfinite(e.a+e.b+e.c+e.d))return;
 float pi;memcpy(&pi,&e.flags,sizeof(pi));if(!std::isfinite(pi))return;
 Vec p=skycraft::McToSky(e.a,e.b,e.c);cCoord* bone=actor;float best=1e30f;
 unsigned count=0;for(auto part=actor->childParts_F4;part&&count++<256;part=part->nextParts_F4){Vec origin{part->mat_C[0][3],part->mat_C[1][3],part->mat_C[2][3]};float d=dot(origin-p,origin-p);if(d<best){best=d;bone=part;}}
 float y=e.d*.0174532925f;pi*=.0174532925f;Vec dir{std::sin(y)*std::cos(pi),std::sin(pi),std::cos(y)*std::cos(pi)};
 stuck.push_back({actor,actor->guid_F8,actor->childParts_F4,bone,toLocal(bone->mat_C,p,true),toLocal(bone->mat_C,dir,false),e.weapon,GetTickCount64()});while(stuck.size()>160)stuck.pop_front();
}
void NativeBegin(){if(owns&&!worldDrawn)++missedWorldFrames;worldActive=worldDrawn=false;nativeWorldDraws=0;}
void NativeEnd(IDirect3DDevice9* d){finishWorld(d,"end-scene");}
void NativeClear(IDirect3DDevice9* d,unsigned flags){if(flags&(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER))finishWorld(d,"clear");}
void NativeTarget(IDirect3DDevice9* d,unsigned index,IDirect3DSurface9* next){
 if(index||!worldActive)return;ComPtr<IDirect3DSurface9> current;
 if(SUCCEEDED(d->GetRenderTarget(0,&current))&&current.Get()!=next)finishWorld(d,"target-change");
}
void NativeDraw(IDirect3DDevice9* d){
 if(!MinecraftVisible()||worldDrawn||!locateGx())return;
 if(*gxType!=0){finishWorld(d,"orthographic");return;}
 if(!queuedCameraValid)return;
 DWORD z=0;d->GetRenderState(D3DRS_ZENABLE,&z);if(!z)return;
 ++nativeWorldDraws;if(worldActive)return;
 ComPtr<IDirect3DSurface9> depth;if(FAILED(d->GetDepthStencilSurface(&depth))||!depth)return;
 if(FAILED(d->GetViewport(&worldViewport))||!worldViewport.Width||!worldViewport.Height)return;
 drawCamera=queuedCamera;memcpy(worldProjection.view,drawCamera.view,sizeof(worldProjection.view));
 memcpy(worldProjection.projection,gxProjection,sizeof(worldProjection.projection));
 memcpy(worldProjection.correction,gxCorrection,sizeof(worldProjection.correction));
 worldProjection.x=worldViewport.X;worldProjection.y=worldViewport.Y;worldProjection.width=worldViewport.Width;worldProjection.height=worldViewport.Height;
 worldProjection.minZ=worldViewport.MinZ;worldProjection.maxZ=worldViewport.MaxZ;
 d->GetRenderState(D3DRS_ZFUNC,&worldDepthFunc);
 if(worldDepthFunc==D3DCMP_LESS)worldDepthFunc=D3DCMP_LESSEQUAL;
 worldActive=true;
 skycraft::Link::Get().DrainRender([&](unsigned type,const uint8_t* p,unsigned n){receive(d,type,p,n);},16ull<<20);
}
void RenderGuest(IDirect3DDevice9* d){
 if(!d)return;D3DVIEWPORT9 vp{};if(SUCCEEDED(d->GetViewport(&vp))){viewportW=vp.Width;viewportH=vp.Height;}
 auto& link=skycraft::Link::Get();if(!link.Valid())return;link.Heartbeat();
 link.DrainRender([&](unsigned type,const uint8_t* p,unsigned n){receive(d,type,p,n);},16ull<<20);
 updateOverlay(d);
 if(!MinecraftVisible()||!link.McAlive())return;
 ComPtr<IDirect3DStateBlock9> saved;if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&saved)))return;saved->Capture();setup(d);
 if(overlay){
  d->SetTexture(0,overlay.Get());d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
  float w=float(viewportW)-.5f,h=float(viewportH)-.5f,t=flip?1.f:0.f,b=flip?0.f:1.f;
  Vertex q[6]={{-.5f,-.5f,0,1,0xffffffff,0,t},{w,-.5f,0,1,0xffffffff,1,t},{w,h,0,1,0xffffffff,1,b},{-.5f,-.5f,0,1,0xffffffff,0,t},{w,h,0,1,0xffffffff,1,b},{-.5f,h,0,1,0xffffffff,0,b}};
  d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,q,sizeof(Vertex));
 }saved->Apply();
 if(guest.flags&P::kMcScreenOpen){auto dl=ImGui::GetForegroundDrawList();dl->AddTriangleFilled({cursorX,cursorY},{cursorX,cursorY+16},{cursorX+10,cursorY+12},IM_COL32(255,255,255,255));}
}
}

