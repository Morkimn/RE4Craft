#include "../src/bridge/Projection.h"
#include "../src/bridge/SolidGrid.h"
#include "../src/bridge/SurfaceVoxels.h"
#include "../src/bridge/ToggleKey.h"
#include "../src/bridge/HitGeometry.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
 using re4craft::segmentCapsule;
 // A hit crosses the body even when its starting point is outside it.
 assert(std::abs(segmentCapsule({-3,1,0},{3,1,0},{0,0,0},{0,2,0},.25)-2.75/6)<1e-9);
 assert(!std::isfinite(segmentCapsule({-3,3,0},{3,3,0},{0,0,0},{0,2,0},.25)));
 assert(std::abs(segmentCapsule({0,4,0},{0,-2,0},{0,0,0},{0,2,0},.25)-1.75/6)<1e-9);
 assert(segmentCapsule({0,1,0},{3,1,0},{0,0,0},{0,2,0},.25)==0);
 assert(std::abs(segmentCapsule({-2,0,0},{2,0,0},{0,0,0},{0,0,0},.5)-.375)<1e-9);
 assert(!std::isfinite(segmentCapsule({-2,0,0},{-1,0,0},{0,0,0},{0,0,0},.5)));
 // World coordinates near the campaign's first house must keep precision.
 assert(std::abs(segmentCapsule({-79283,860,-40208},{-79277,860,-40208},{-79280,859,-40208},{-79280,861,-40208},.25)-2.75/6)<1e-9);
 double body=segmentCapsule({-3,1,0},{3,1,0},{0,.4,0},{0,1.2,0},.25);
 double head=segmentCapsule({-3,1,0},{3,1,0},{0,1.6,0},{0,1.7,0},.2);
 assert(std::isfinite(body)&&!std::isfinite(head)); // body cannot become a headshot
 re4craft::ToggleKey key;assert(key.update(true));assert(!key.update(true));assert(!key.update(true)); // held-key repeats
 assert(!key.update(false));assert(!key.update(false));assert(key.update(true));key.reset();assert(key.update(true));
 re4craft::Projection p;
 p.view[0]=p.view[5]=p.view[10]=1;
 // Offline-verified GX perspective conversion, near=100 and far=1,000,000.
 p.projection[0]=1.071111f;p.projection[5]=1.428148f;
 p.projection[10]=-1.00010001f;p.projection[11]=-1;
 p.projection[14]=-100.010002f;
 p.correction[0]=p.correction[10]=p.correction[15]=1;p.correction[5]=4.f/3;
 p.width=1920;p.height=1080;
 float x,y,z,rhw;
 assert(p.project(0,0,-5000,x,y,z,rhw));
 assert(std::abs(x-959.5f)<.01f&&std::abs(y-539.5f)<.01f);
 assert(std::abs(z-.980098f)<.00001f); // Must share native depth, not ~0.49.
 assert(p.project(1000,500,-5000,x,y,z,rhw));
 assert(std::abs(x-1165.1533f)<.01f&&std::abs(y-436.67334f)<.01f);
 // A static world point stays in place when camera and point translate equally.
 auto oldX=x,oldY=y;p.view[3]=-12000;p.view[7]=3000;p.view[11]=-8000;
 assert(p.project(13000,-2500,3000,x,y,z,rhw));
 assert(std::abs(x-oldX)<.01f&&std::abs(y-oldY)<.01f);
 assert(!p.project(12000,-3000,8100,x,y,z,rhw)); // Behind the camera.
 assert(!p.project(12000,-3000,7950,x,y,z,rhw)); // Inside the near plane.
 // A face crossing the near plane remains a clipped polygon, with UVs
 // interpolated at the new edge, rather than disappearing for one frame.
 re4craft::ClipVertex tri[3]{};tri[0].p[0]=-.5f;tri[0].p[2]=-.5f;tri[0].p[3]=1;
 tri[1].p[0]=.5f;tri[1].p[2]=.5f;tri[1].p[3]=1;tri[1].u=1;
 tri[2].p[1]=.5f;tri[2].p[2]=.5f;tri[2].p[3]=1;tri[2].v=1;
 re4craft::ClipVertex clipped[12];int n=re4craft::clipTriangle(tri,clipped);assert(n==4);
 for(int i=0;i<n;i++){assert(clipped[i].p[2]>=0);assert(std::isfinite(clipped[i].p[0]));}
 re4craft::SolidGrid grid;grid.sections[{0,-32,0}][0]=1; // (0,-512,0)
 assert(grid.occupied(0,-512,0)&&!grid.occupied(-1,-512,0));
 auto q=grid.sweep({-2,-512,.5},{2,-512,.5},.25,1.8);assert(std::abs(q.x+.25)<.002); // fast motion cannot tunnel
 q=grid.sweep({-2,-512,-.1},{.5,-512,.6},.25,1.8);assert(q.x<-.249&&q.z>.599); // slide along wall
 q=grid.sweep({.5,-510,.5},{.5,-513,.5},.25,1.8);assert(std::abs(q.y+511)<.002); // land on block
 q=grid.sweep({.5,-512,.5},{.5,-512,.5},.25,1.8);assert(!grid.occupied(int(std::floor(q.x)),int(std::floor(q.y)),int(std::floor(q.z))));
 q=grid.sweep({-2,-512,-2},{2,-512,2},.25,1.8);assert(q.x<1.5||q.z<1.5); // collision deflects fast diagonal; slide can pass the corner
 grid.sections[{0,-32,0}][0]|=2|16; // adjacent cells (1,-512,0), (4,-512,0)
 q=grid.sweep({.95,-512,.5},{.95,-512,.5},.25,1.8);assert(!grid.intersects(q,.25,1.8)); // build around actor
 grid.sections.clear();q=grid.sweep({-2,-512,.5},{2,-512,.5},.25,1.8);assert(std::abs(q.x-2)<1e-8); // mined blocks stop colliding
 // A large native floor lies exactly on the boundary y=-512. Every subcell
 // immediately below it must support small props and floor-mounted doors.
 uint64_t floor[8][8]{};
 float groundA[]={-20,-512,-20,20,-512,-20,20,-512,20},groundB[]={-20,-512,-20,20,-512,20,-20,-512,20};
 auto voxel=[&](int bx,int by,int bz,int vx,int vy,int vz){assert(by==-513&&vy==7);floor[bx][bz]|=uint64_t(1)<<(vz*8+vx);};
 re4craft::rasterSurface(groundA,0,-520,0,voxel);re4craft::rasterSurface(groundB,0,-520,0,voxel);
 for(auto& row:floor)for(auto bits:row)assert(bits==UINT64_MAX);
 int upperCells=0;re4craft::rasterSurface(groundA,0,-512,0,[&](int,int,int,int,int,int){++upperCells;});assert(upperCells==0);
 float slope[]={0,-511.75,0,8,-509.75,0,0,-511.75,8};int slopeCells=0;
 re4craft::rasterSurface(slope,0,-512,0,[&](int bx,int by,int bz,int vx,int vy,int vz){assert(bx>=0&&bx<8&&by>=-512&&by< -504&&bz>=0&&bz<8);++slopeCells;});assert(slopeCells>2000);
 std::cout<<"Native projection/depth regression checks passed\n";
}
