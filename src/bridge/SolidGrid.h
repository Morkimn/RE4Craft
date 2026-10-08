#pragma once
#include <array>
#include <map>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace re4craft {
// Minecraft coordinates. Full block occupancy is exported by the guest;
// native actor movement is swept against it before publishing the next state.
struct SolidGrid {
 using Key=std::tuple<int,int,int>;
 std::map<Key,std::array<uint8_t,512>> sections;
 struct Point { double x,y,z; };
 bool occupied(int x,int y,int z) const {
  const int sx=int(std::floor(x/16.)),sy=int(std::floor(y/16.)),sz=int(std::floor(z/16.));
  auto i=sections.find({sx,sy,sz});if(i==sections.end())return false;
  const unsigned bit=unsigned(x-sx*16)+16*unsigned(z-sz*16)+256*unsigned(y-sy*16);
  return (i->second[bit/8]&(1u<<(bit%8)))!=0;
 }
 template<class F> void cells(Point lo,Point hi,F f) const {
  if(!std::isfinite(lo.x+lo.y+lo.z+hi.x+hi.y+hi.z))return;
  int xmin=int(std::floor(lo.x)),xmax=int(std::floor(hi.x));
  int ymin=int(std::floor(lo.y)),ymax=int(std::floor(hi.y));
  int zmin=int(std::floor(lo.z)),zmax=int(std::floor(hi.z));
  if(xmax-xmin>64||ymax-ymin>32||zmax-zmin>64)return;
  for(int y=ymin;y<=ymax;y++)for(int z=zmin;z<=zmax;z++)for(int x=xmin;x<=xmax;x++)if(occupied(x,y,z))f(x,y,z);
 }
 bool intersects(Point p,double radius,double height) const {
  bool hit=false;cells({p.x-radius+.0005,p.y+.0005,p.z-radius+.0005},{p.x+radius-.0005,p.y+height-.0005,p.z+radius-.0005},[&](int,int,int){hit=true;});return hit;
 }
 Point sweepStep(Point from,Point to,double radius,double height) const {
  constexpr double eps=.0005;
  radius=std::clamp(radius,.05,3.);height=std::clamp(height,.1,12.);
  Point p=from;
  // Y first lets grounded actors slide along walls and land on constructions.
  for(int axis:{1,0,2}) {
   double delta=axis==0?to.x-from.x:axis==1?to.y-from.y:to.z-from.z;
   if(std::abs(delta)<1e-10)continue;
   Point lo{p.x-radius+eps,p.y+eps,p.z-radius+eps},hi{p.x+radius-eps,p.y+height-eps,p.z+radius-eps};
   Point a=lo,b=hi;
   if(axis==0){a.x+=std::min(0.,delta);b.x+=std::max(0.,delta);}
   if(axis==1){a.y+=std::min(0.,delta);b.y+=std::max(0.,delta);}
   if(axis==2){a.z+=std::min(0.,delta);b.z+=std::max(0.,delta);}
   cells(a,b,[&](int x,int y,int z){
    double mn[3]={double(x),double(y),double(z)},mx[3]={x+1.,y+1.,z+1.};
    double low[3]={lo.x,lo.y,lo.z},high[3]={hi.x,hi.y,hi.z};
    for(int j=0;j<3;j++)if(j!=axis&&(high[j]<=mn[j]||low[j]>=mx[j]))return;
    if(delta>0&&high[axis]<=mn[axis]+eps)delta=std::min(delta,std::max(0.,mn[axis]-high[axis]-eps));
    if(delta<0&&low[axis]>=mx[axis]-eps)delta=std::max(delta,std::min(0.,mx[axis]-low[axis]+eps));
   });
   if(axis==0)p.x+=delta;if(axis==1)p.y+=delta;if(axis==2)p.z+=delta;
  }
  // A freshly placed block may overlap an actor. Resolve to the nearest
  // horizontal free face; never push down through the native terrain.
  if(intersects(p,radius,height)){
   // Search all five escape directions through contiguous blocks. Resolving
   // just one cell can oscillate between neighbours in a thick wall.
   Point directions[]={{-1,0,0},{1,0,0},{0,0,-1},{0,0,1},{0,1,0}};
   Point best=p;double distance=1e30;
   for(auto dir:directions)for(double d=.0625;d<=12&&d<distance;d+=.0625){
    Point candidate{p.x+dir.x*d,p.y+dir.y*d,p.z+dir.z*d};
    if(!intersects(candidate,radius,height)){best=candidate;distance=d;break;}
   }p=best;
  }
  return p;
 }
 Point sweep(Point from,Point to,double radius,double height) const {
  if(sections.empty())return to;
  radius=std::clamp(radius,.05,3.);height=std::clamp(height,.1,12.);
  Point delta{to.x-from.x,to.y-from.y,to.z-from.z};
  if(!std::isfinite(delta.x+delta.y+delta.z))return from;
  // Bounded substeps also sweep a fast diagonal through a wall corner. One
  // large X-then-Z move could otherwise route around the cell between frames.
  const double length=std::max({std::abs(delta.x),std::abs(delta.y),std::abs(delta.z)});
  const int steps=std::clamp(int(std::ceil(length/std::min(.25,radius*.5))),1,640);
  delta.x/=steps;delta.y/=steps;delta.z/=steps;
  Point p=from;
  for(int i=0;i<steps;i++)p=sweepStep(p,{p.x+delta.x,p.y+delta.y,p.z+delta.z},radius,height);
  return p;
 }
};
}
