#pragma once
#include <cmath>
namespace re4craft {
struct ClipVertex { float p[4]{},u=0,v=0,c[4]{}; };
// Sutherland-Hodgman in homogeneous D3D clip space. Rejecting a whole
// triangle when one vertex crosses the near plane causes visible popping.
inline int clipTriangle(const ClipVertex* in,ClipVertex* out) {
 ClipVertex a[12],b[12];for(int i=0;i<3;i++)a[i]=in[i];int count=3;
 for(int plane=0;plane<6&&count;plane++){
  auto distance=[plane](const ClipVertex& v){switch(plane){case 0:return v.p[2];case 1:return v.p[3]-v.p[2];case 2:return v.p[3]+v.p[0];case 3:return v.p[3]-v.p[0];case 4:return v.p[3]+v.p[1];default:return v.p[3]-v.p[1];}};
  int n=0;auto previous=a[count-1];float dp=distance(previous);
  for(int i=0;i<count;i++){
   auto current=a[i];float dc=distance(current);
   if((dp>=0)!=(dc>=0)){
    float t=dp/(dp-dc);ClipVertex v;
    for(int j=0;j<4;j++){v.p[j]=previous.p[j]+t*(current.p[j]-previous.p[j]);v.c[j]=previous.c[j]+t*(current.c[j]-previous.c[j]);}
    v.u=previous.u+t*(current.u-previous.u);v.v=previous.v+t*(current.v-previous.v);b[n++]=v;
   }
   if(dc>=0)b[n++]=current;previous=current;dp=dc;
  }count=n;for(int i=0;i<count;i++)a[i]=b[i];
 }for(int i=0;i<count;i++)out[i]=a[i];return count;
}
// GX's converted projection and viewport correction are column-major D3D
// matrices. The SDK camera view is a row-major 3x4 matrix.
struct Projection {
 float view[12]{},projection[16]{},correction[16]{};
 unsigned x=0,y=0,width=0,height=0;
 float minZ=0,maxZ=1;
 void clip(float wx,float wy,float wz,float* corrected) const {
  float v[4]={0,0,0,1},c[4]{};
  for(int i=0;i<3;i++)v[i]=view[i*4]*wx+view[i*4+1]*wy+view[i*4+2]*wz+view[i*4+3];
  for(int i=0;i<4;i++)for(int j=0;j<4;j++)c[i]+=projection[j*4+i]*v[j];
  for(int i=0;i<4;i++){corrected[i]=0;for(int j=0;j<4;j++)corrected[i]+=correction[j*4+i]*c[j];}
 }
 bool project(float wx,float wy,float wz,float& xOut,float& yOut,float& zOut,float& rhwOut) const {
  float v[4]={0,0,0,1},clip[4]{},corrected[4]{};
  for(int i=0;i<3;i++)v[i]=view[i*4]*wx+view[i*4+1]*wy+view[i*4+2]*wz+view[i*4+3];
  for(int i=0;i<4;i++)for(int j=0;j<4;j++)clip[i]+=projection[j*4+i]*v[j];
  for(int i=0;i<4;i++)for(int j=0;j<4;j++)corrected[i]+=correction[j*4+i]*clip[j];
  const float w=corrected[3];
  if(!std::isfinite(w)||w<=0)return false;
  const float depth=corrected[2]/w;
  if(!std::isfinite(depth)||depth<0||depth>1)return false;
  xOut=x+(corrected[0]/w*.5f+.5f)*width-.5f;
  yOut=y+(.5f-corrected[1]/w*.5f)*height-.5f;
  zOut=minZ+depth*(maxZ-minZ);rhwOut=1.f/w;
  return std::isfinite(xOut)&&std::isfinite(yOut);
 }
};
}
