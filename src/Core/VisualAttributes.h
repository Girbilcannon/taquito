#pragma once
#include "MarkerPack.h"
#include <algorithm>
#include <cmath>

namespace VisualAttributes {
constexpr float WorldTrailBaseWidth=1.016f; // 40 game inches, full ribbon width.
inline float WorldMarkerHalfSize(float focal,float depth,const TacoPack::Style& style,float multiplier) {
    if(!(depth>0) || !std::isfinite(depth)) return 0;
    // TacO's projected iconSize is a half-extent; the full quad is 2*iconSize meters.
    return std::clamp(focal*style.iconSize*multiplier/depth,style.minSize,style.maxSize);
}
inline float WorldTrailWidth(float trailScale,float multiplier) {
    return WorldTrailBaseWidth*std::max(0.0f,trailScale)*multiplier;
}
inline float MapTrailWidth(float displaySize,float trailScale) {
    // Return the full ribbon width; MapTrailRibbon applies the half-extent.
    return std::clamp(displaySize*trailScale,1.0f,100.0f);
}
inline float DistanceScale(float value) { return std::isfinite(value)?std::clamp(value,0.25f,4.0f):1.0f; }
struct FadeBand {float nearDistance,farDistance;};
inline FadeBand ShiftedFade(const TacoPack::Style& style,float factor) {
    factor=DistanceScale(factor);
    if(style.distanceFadeDisabled)return {style.fadeNear,style.fadeFar};
    if(style.fadeNear<0 || style.fadeFar<0)return {3000*factor,3000*factor+500};
    if(style.fadeFar<=style.fadeNear)return {style.fadeNear*factor,style.fadeFar*factor};
    const float start=style.fadeNear*factor;
    return {start,start+(style.fadeFar-style.fadeNear)};
}
inline void ScaleFade(TacoPack::Style& style,float factor) {
    const auto band=ShiftedFade(style,factor);
    style.fadeNear=band.nearDistance;style.fadeFar=band.farDistance;
}
inline float Opacity(const TacoPack::Style& style, float meters,float distanceScale=1.0f) {
    const float alpha=std::clamp(style.alpha,0.0f,1.0f);
    if(style.distanceFadeDisabled)return alpha;
    const auto band=ShiftedFade(style,distanceScale);
    const float inches=meters/.0254f;
    if(band.farDistance<=band.nearDistance) return inches<band.farDistance ? alpha : 0.0f;
    return alpha*std::clamp((band.farDistance-inches)/(band.farDistance-band.nearDistance),0.0f,1.0f);
}
inline double TileLength(float referenceLength,int /*imageWidth*/,int /*imageHeight*/,double minimum) {
    // One complete texture repeat maps to the logical reference length, independent
    // of bitmap resolution or aspect ratio. Preserve the entire texture UV range.
    return std::max(minimum,static_cast<double>(referenceLength));
}
inline double AnimationOffset(double seconds,float speed,double tileLength) {
    return -seconds*speed*tileLength; // speed is texture repeats per second.
}
inline TacoPack::Vec3 Rotate(TacoPack::Vec3 p,TacoPack::Vec3 degrees) {
    constexpr float radians=3.14159265358979323846f/180.0f;
    const float cx=std::cos(degrees.x*radians),sx=std::sin(degrees.x*radians);
    const float cy=std::cos(degrees.y*radians),sy=std::sin(degrees.y*radians);
    const float cz=std::cos(degrees.z*radians),sz=std::sin(degrees.z*radians);
    p={p.x,p.y*cx-p.z*sx,p.y*sx+p.z*cx};
    p={p.x*cy+p.z*sy,p.y,-p.x*sy+p.z*cy};
    return {p.x*cz-p.y*sz,p.x*sz+p.y*cz,p.z};
}
inline TacoPack::Vec3 WorldMarkerOffset(TacoPack::Vec3 imagePoint,TacoPack::Vec3 degrees) {
    // XML rotation follows Pathing's Z-up image plane, not Mumble's Y-up axes.
    // Pathing's U=0 edge is +X; our image-local U=0 edge is -X. Match its
    // texture orientation before rotating, then convert the resulting offset
    // back to Mumble coordinates. Positions/height offsets are already Y-up.
    const auto rotated=Rotate({-imagePoint.x,imagePoint.y,imagePoint.z},degrees);
    return {rotated.x,rotated.z,rotated.y};
}
// Signed triangle area in screen coordinates (+Y down): positive is clockwise.
// Cull the named winding, matching Pathing's rasterizer convention. Maps/sticky
// images never use this test; no shared GPU rasterizer state is changed.
inline bool Culled(TacoPack::Style::Cull mode,float signedArea) {
    if(mode==TacoPack::Style::Cull::None)return false;
    if(!std::isfinite(signedArea) || signedArea==0)return true;
    return mode==TacoPack::Style::Cull::Clockwise ? signedArea>0 : signedArea<0;
}
}
