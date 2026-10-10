#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace re4craft {
struct HitPoint {
 double x=0,y=0,z=0;
 HitPoint operator+(HitPoint b)const{return {x+b.x,y+b.y,z+b.z};}
 HitPoint operator-(HitPoint b)const{return {x-b.x,y-b.y,z-b.z};}
 HitPoint operator*(double k)const{return {x*k,y*k,z*k};}
};
inline double hitDot(HitPoint a,HitPoint b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline double pointSegmentDistanceSq(HitPoint p,HitPoint a,HitPoint b){
 auto d=b-a;double l=hitDot(d,d),t=l>1e-12?std::clamp(hitDot(p-a,d)/l,0.,1.):0.;
 auto q=p-(a+d*t);return hitDot(q,q);
}
// First contact along a finite segment with an animated native body capsule.
// The old native query tested only the start point, not this whole segment.
inline double segmentCapsule(HitPoint from,HitPoint to,HitPoint a,HitPoint b,double radius){
 const double miss=std::numeric_limits<double>::infinity();
 if(!(radius>0)||!std::isfinite(radius))return miss;
 if(pointSegmentDistanceSq(from,a,b)<=radius*radius)return 0;
 auto d=to-from,axis=b-a,offset=from-a;
 double dd=hitDot(d,d),aa=hitDot(axis,axis),best=miss;
 if(dd<1e-12)return miss;
 auto sphere=[&](HitPoint center){
  auto m=from-center;double md=hitDot(m,d),c=hitDot(m,m)-radius*radius,disc=md*md-dd*c;
  if(disc<0)return;
  double t=(-md-std::sqrt(std::max(0.,disc)))/dd;
  if(t>=0&&t<=1)best=std::min(best,t);
 };
 sphere(a);sphere(b);
 if(aa>1e-12){
  double ad=hitDot(axis,d),ao=hitDot(axis,offset);
  double qa=dd-ad*ad/aa,qb=hitDot(d,offset)-ad*ao/aa;
  double qc=hitDot(offset,offset)-ao*ao/aa-radius*radius,disc=qb*qb-qa*qc;
  if(qa>1e-12&&disc>=0){
   double root=std::sqrt(std::max(0.,disc));
   for(double t:{(-qb-root)/qa,(-qb+root)/qa}){
    double along=(ao+t*ad)/aa;
    if(t>=0&&t<=1&&along>=0&&along<=1)best=std::min(best,t);
   }
  }
 }
 return best;
}
}
