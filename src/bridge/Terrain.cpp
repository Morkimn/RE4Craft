#include "../dllmain.h"
#include "State.h"
#include "SurfaceVoxels.h"
#include <cmath>
#include <tuple>
#include <algorithm>
#include <cstring>
#include <chrono>
namespace re4craft {
namespace {
using Region=std::tuple<int,int,int>;
std::vector<P::ColTri> triangles;
std::vector<Region> regions;
size_t next=0;
ULONGLONG refreshAt=0;
Region center{};
std::map<Region,uint64_t> sentHashes;
unsigned sentRegions=0,unchangedRegions=0;double longestSendMs=0;
Vec transform(const Mtx& m,const Vec& v){return {
 m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z+m[0][3],
 m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z+m[1][3],
 m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z+m[2][3]};}
void collect(){
 triangles.clear();
 // Source: native SDK and DebugDisplay.cpp:cSat__disp (indices 0..2, mat_60).
 for(auto mgr:{SatMgr,EatMgr}){
  if(!mgr||mgr->m_nArray_8>4096)continue;
  for(auto& sat:*mgr){
   if(!sat.IsValid()||!(sat.m_Flag_2A&cSat::FLAG_ENABLE_VAL)||!sat.vert_p_C||!sat.poly_p_18)continue;
   for(unsigned i=0;i<sat.polygon_num_1E&&triangles.size()<200000;i++){
    P::ColTri t{};bool valid=true;
    for(int k=0;k<3;k++){unsigned index=sat.poly_p_18[i][k];if(index>=sat.vertex_num_1C){valid=false;break;}
     auto q=skycraft::SkyToMc(transform(sat.mat_60,sat.vert_p_C[index]));
     if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)){valid=false;break;}
     t.v[k*3]=float(q.x);t.v[k*3+1]=float(q.y);t.v[k*3+2]=float(q.z);
    }if(valid)triangles.push_back(t);
   }
  }
 }spd::log()->info("RE4CRAFT collision: {} native triangles",triangles.size());
}
void schedule(Region pos){
 center=pos;auto [x,y,z]=pos;regions.clear();next=0;
 for(int dx=-3;dx<=3;dx++)for(int dy=-2;dy<=2;dy++)for(int dz=-3;dz<=3;dz++)regions.emplace_back(x+dx,y+dy,z+dz);
 auto dist=[=](Region r){auto [rx,ry,rz]=r;return (rx-x)*(rx-x)+(ry-y)*(ry-y)+(rz-z)*(rz-z);};
 std::sort(regions.begin(),regions.end(),[&](Region a,Region b){return dist(a)<dist(b);});
}
bool overlaps(const P::ColTri& t,int x,int y,int z){
 // Include the cell immediately below an exact region-boundary surface.
 for(int a=0;a<3;a++){float lo=std::min({t.v[a],t.v[3+a],t.v[6+a]}),hi=std::max({t.v[a],t.v[3+a],t.v[6+a]});int origin=a==0?x:a==1?y:z;if(hi<origin||lo>origin+8+(a==1?.03f:0.f))return false;}return true;
}
bool send(Region region,unsigned epoch){
 auto [rx,ry,rz]=region;int x=rx*8,y=ry*8,z=rz*8;auto& link=skycraft::Link::Get();
 std::vector<P::ColTri> localTris;for(auto& t:triangles)if(overlaps(t,x,y,z))localTris.push_back(t);
 uint64_t hash=1469598103934665603ull;
 for(auto& t:localTris){auto bytes=reinterpret_cast<const uint8_t*>(&t);for(size_t i=0;i<sizeof(t);i++)hash=(hash^bytes[i])*1099511628211ull;}
 auto old=sentHashes.find(region);if(old!=sentHashes.end()&&old->second==hash){++unchangedRegions;return true;}
 auto started=std::chrono::steady_clock::now();
 P::ColRegion r{x,y,z,x+7,y+7,z+7,epoch,unsigned(localTris.size())};
 std::vector<uint8_t> message(sizeof(r)+localTris.size()*sizeof(P::ColTri));memcpy(message.data(),&r,sizeof(r));
 if(!localTris.empty())memcpy(message.data()+sizeof(r),localTris.data(),localTris.size()*sizeof(P::ColTri));
 if(!link.WriteCollision(P::kColTris,message.data(),unsigned(message.size())))return false;
 // Surface voxels support vanilla interaction queries; smooth motion uses the triangles.
 std::map<std::tuple<int,int,int>,P::ColBlock> cells;
 for(auto& t:localTris)rasterSurface(t.v,x,y,z,[&](int bx,int by,int bz,int vx,int vy,int vz){auto& block=cells[{bx,by,bz}];block.x=bx;block.y=by;block.z=bz;block.bits[vy]|=uint64_t(1)<<(vz*8+vx);});
 r.count=unsigned(cells.size());message.resize(sizeof(r)+cells.size()*sizeof(P::ColBlock));memcpy(message.data(),&r,sizeof(r));size_t n=sizeof(r);
 for(auto& [key,cell]:cells){memcpy(message.data()+n,&cell,sizeof(cell));n+=sizeof(cell);}
 if(!link.WriteCollision(P::kColRegion,message.data(),unsigned(message.size())))return false;
 sentHashes[region]=hash;++sentRegions;longestSendMs=std::max(longestSendMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());return true;
}
}
bool TerrainGround(float x,float z,float ceiling,float& y){
 bool found=false;float highest=-1e20f;
 for(const auto& t:triangles){
  float ax=t.v[0],ay=t.v[1],az=t.v[2],bx=t.v[3],by=t.v[4],bz=t.v[5],cx=t.v[6],cy=t.v[7],cz=t.v[8];
  float denom=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz);if(std::abs(denom)<1e-6f)continue;
  float u=((bz-cz)*(x-cx)+(cx-bx)*(z-cz))/denom,v=((cz-az)*(x-cx)+(ax-cx)*(z-cz))/denom;
  if(u<-.0001f||v<-.0001f||u+v>1.0001f)continue;
  float h=u*ay+v*by+(1-u-v)*cy;if(h>ceiling||h<ceiling-16)continue;
  if(h>highest){highest=h;found=true;}
 }if(found)y=highest;return found;
}
void TerrainReset(unsigned epoch){triangles.clear();regions.clear();sentHashes.clear();next=0;refreshAt=0;skycraft::Link::Get().WriteCollision(P::kColClear,&epoch,sizeof(epoch));}
void TerrainUpdate(Vec feet,unsigned epoch,bool refresh){
 auto m=skycraft::SkyToMc(feet);Region pos{int(std::floor(m.x/8)),int(std::floor(m.y/8)),int(std::floor(m.z/8))};
 if(refresh||triangles.empty()||(next>=regions.size()&&GetTickCount64()-refreshAt>3000)){collect();schedule(pos);refreshAt=GetTickCount64();}
 else if(pos!=center)schedule(pos);
 auto start=std::chrono::steady_clock::now();int count=0;
 while(next<regions.size()&&count++<2){if(!send(regions[next],epoch))break;++next;if(std::chrono::steady_clock::now()-start>std::chrono::milliseconds(4))break;}
 static ULONGLONG lastStats=0;if(GetTickCount64()-lastStats>10000){lastStats=GetTickCount64();spd::log()->info("RE4CRAFT terrain sent={} unchanged={} maxRegionMs={:.2f}",sentRegions,unchangedRegions,longestSendMs);}
}
}


