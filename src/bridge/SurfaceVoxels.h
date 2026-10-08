#pragma once
#include <algorithm>
#include <cmath>
namespace re4craft {
namespace surface {
struct Point { double p[3]; };
inline int clip(const Point* in,int count,Point* out,int axis,double edge,bool keepAbove){
 int n=0;if(!count)return 0;
 auto prev=in[count-1];double pd=(prev.p[axis]-edge)*(keepAbove?1:-1);bool pi=pd>=-1e-9;
 for(int i=0;i<count;i++){
  auto cur=in[i];double cd=(cur.p[axis]-edge)*(keepAbove?1:-1);bool ci=cd>=-1e-9;
  if(pi!=ci){double t=pd/(pd-cd);Point q{};for(int k=0;k<3;k++)q.p[k]=prev.p[k]+t*(cur.p[k]-prev.p[k]);out[n++]=q;}
  if(ci)out[n++]=cur;prev=cur;pd=cd;pi=ci;
 }return n;
}
}
// Conservatively rasterize a triangle into 1/8-block surface voxels. Clip to
// the region before rasterization: huge native polygons cost at most 64x64
// projected cells here. Testing coverage, rather than scattered points,
// prevents small item colliders from falling through gaps in the surface.
template<class F> void rasterSurface(const float* vertices,int x,int y,int z,F put){
 using surface::Point;Point poly[16]{},temp[16]{};int count=3;
 const int origin[3]={x,y,z};
 for(int i=0;i<3;i++)for(int k=0;k<3;k++)poly[i].p[k]=vertices[i*3+k]-(k==1?.03:0);
 double ab[3],ac[3],normal[3];for(int k=0;k<3;k++){ab[k]=poly[1].p[k]-poly[0].p[k];ac[k]=poly[2].p[k]-poly[0].p[k];}
 for(int k=0;k<3;k++)normal[k]=ab[(k+1)%3]*ac[(k+2)%3]-ab[(k+2)%3]*ac[(k+1)%3];
 int axis=0;for(int k=1;k<3;k++)if(std::abs(normal[k])>std::abs(normal[axis]))axis=k;
 if(std::abs(normal[axis])<1e-9)return;
 for(int k=0;k<3;k++)for(int side=0;side<2;side++){
  count=surface::clip(poly,count,temp,k,origin[k]+side*8,side==0);std::copy(temp,temp+count,poly);if(count<3)return;
 }
 int u=(axis+1)%3,v=(axis+2)%3;double low[3]={1e30,1e30,1e30},high[3]={-1e30,-1e30,-1e30};
 for(int i=0;i<count;i++)for(int k=0;k<3;k++){low[k]=std::min(low[k],poly[i].p[k]);high[k]=std::max(high[k],poly[i].p[k]);}
 auto cell=[&](int k,double a){return std::clamp(int(std::floor((a-origin[k])*8)),0,63);};
 for(int iu=cell(u,low[u]);iu<=cell(u,high[u]);iu++)for(int iv=cell(v,low[v]);iv<=cell(v,high[v]);iv++){
  Point a[16],b[16];std::copy(poly,poly+count,a);int n=count;
  for(int k:{u,v})for(int side=0;side<2;side++){
   double edge=origin[k]+((k==u?iu:iv)+side)/8.;n=surface::clip(a,n,b,k,edge,side==0);std::copy(b,b+n,a);
  }
  if(n<3)continue;
  double lo=1e30,hi=-1e30;for(int i=0;i<n;i++){lo=std::min(lo,a[i].p[axis]);hi=std::max(hi,a[i].p[axis]);}
  int first=cell(axis,lo+(hi-lo>1e-7?1e-7:0)),last=cell(axis,hi-(hi-lo>1e-7?1e-7:0));
  for(int id=first;id<=last;id++){
   int c[3];c[axis]=id;c[u]=iu;c[v]=iv;
   put(x+c[0]/8,y+c[1]/8,z+c[2]/8,c[0]%8,c[1]%8,c[2]%8);
  }
 }
}
}
