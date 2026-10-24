#include "presentation/windows/smo_duodecimal_scene.hpp"
#include "presentation/windows/smo_float12.hpp"
#include <algorithm>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
void validate(const SmoD12SceneWorkload& work) {
    if(!work.bodies||work.bodies>16||!work.width||work.width>64||!work.height||work.height>36)
        throw std::invalid_argument("La escena duodecimal está acotada a 16 cuerpos y 64 por 36 píxeles.");
}
} // namespace
namespace {
template<bool Floating> struct SceneReference {
// Numeric policies share geometry, not numeric representations. Member lookup
// deliberately binds every operation before argument-dependent lookup.
#define SMO_REFERENCE_BIND(result,name,parameters,arguments) \
    result smo_d12_##name parameters {if constexpr(Floating)return smo_f12_##name arguments;else return lgaed::presentation::smo_d12_##name arguments;}
SMO_REFERENCE_BIND(SmoD12,zero,(),())
SMO_REFERENCE_BIND(SmoD12,uint,(std::uint32_t v),(v))
SMO_REFERENCE_BIND(SmoD12,literal,(std::uint32_t w,std::uint32_t f,std::uint32_t c,std::uint32_t n),(w,f,c,n))
SMO_REFERENCE_BIND(bool,valid,(SmoD12 a),(a))
SMO_REFERENCE_BIND(int,compare,(SmoD12 a,SmoD12 b),(a,b))
SMO_REFERENCE_BIND(SmoD12,abs,(SmoD12 a),(a))
SMO_REFERENCE_BIND(SmoD12,negate,(SmoD12 a),(a))
SMO_REFERENCE_BIND(SmoD12,add,(SmoD12 a,SmoD12 b),(a,b))
SMO_REFERENCE_BIND(SmoD12,sub,(SmoD12 a,SmoD12 b),(a,b))
SMO_REFERENCE_BIND(SmoD12,mul,(SmoD12 a,SmoD12 b),(a,b))
SMO_REFERENCE_BIND(SmoD12,pdir,(SmoD12 a,SmoD12 b),(a,b))
SMO_REFERENCE_BIND(SmoD12,pinv,(SmoD12 a,SmoD12 b),(a,b))
SMO_REFERENCE_BIND(SmoD12,sqrt,(SmoD12 a),(a))
SMO_REFERENCE_BIND(SmoD12,magnitude2,(SmoD12 x,SmoD12 y,SmoD12 z),(x,y,z))
SMO_REFERENCE_BIND(SmoD12,norm3,(SmoD12 x,SmoD12 y,SmoD12 z),(x,y,z))
SMO_REFERENCE_BIND(SmoD12,discriminant,(SmoD12 h,SmoD12 a,SmoD12 c),(h,a,c))
SMO_REFERENCE_BIND(SmoD12,clamp,(SmoD12 a,SmoD12 l,SmoD12 h),(a,l,h))
#undef SMO_REFERENCE_BIND
using Vector=std::array<SmoD12,3>;
SmoD12 literal(std::uint32_t whole,std::uint32_t fraction=0,std::uint32_t count=0) {
    return smo_d12_literal(whole,fraction,count,0);
}
SmoD12 one(){return literal(0x1);}
SmoD12 dt(){return literal(0,0x01,2);}
SmoD12 epsilon(){return literal(0,0x0001,4);}
Vector zero3(){return {smo_d12_zero(),smo_d12_zero(),smo_d12_zero()};}
SmoD12 square(SmoD12 value) {
    const auto a=smo_d12_abs(value);
    if(smo_d12_compare(a,smo_d12_zero())==0) {auto result=smo_d12_zero();result.diagnostic=a.diagnostic;return result;}
    return smo_d12_pdir(a,smo_d12_uint(2));
}
Vector add(Vector a,Vector b) {for(unsigned k=0;k<3;++k)a[k]=smo_d12_add(a[k],b[k]);return a;}
Vector subtract(Vector a,Vector b) {for(unsigned k=0;k<3;++k)a[k]=smo_d12_sub(a[k],b[k]);return a;}
Vector scale(Vector a,SmoD12 nonnegative) {for(auto& component:a)component=smo_d12_mul(component,nonnegative);return a;}
Vector divide(Vector a,SmoD12 positive) {return scale(a,smo_d12_pinv(positive,smo_d12_uint(3)));}
SmoD12 magnitude2(Vector a){return smo_d12_magnitude2(a[0],a[1],a[2]);}
SmoD12 norm3(Vector a){return smo_d12_norm3(a[0],a[1],a[2]);}
SmoD12 dot_positive(Vector a,Vector b){return smo_d12_add(smo_d12_add(smo_d12_mul(a[0],b[0]),smo_d12_mul(a[1],b[1])),smo_d12_mul(a[2],b[2]));}
std::uint32_t diagnostic(Vector a){return a[0].diagnostic|a[1].diagnostic|a[2].diagnostic;}
std::uint32_t diagnostic(const SmoD12SceneBody& b){return b.diagnostic|diagnostic(b.position)|b.radius.diagnostic|diagnostic(b.velocity);}
std::vector<SmoD12SceneBody> initial(const SmoD12SceneWorkload& work) {
    validate(work);std::vector<SmoD12SceneBody> result(work.bodies);
    for(std::uint32_t index=0;index<work.bodies;++index) {
        auto& b=result[index];const auto column=index%4,row=index/4;
        const auto half=literal(0,0x6,1),one_half=literal(0x1,0x6,1);
        b.position={smo_d12_sub(smo_d12_uint(column),one_half),smo_d12_sub(one_half,smo_d12_uint(row)),
            smo_d12_add(smo_d12_uint(2),smo_d12_mul(smo_d12_uint(index%3),half))};
        b.radius=literal(0,0x2,1);
        auto vx=literal(0,0x06,2),vz=literal(0,0x04,2);
        if(index%2)vx=smo_d12_negate(vx);if(column%2)vz=smo_d12_negate(vz);
        b.velocity={vx,smo_d12_zero(),vz};b.diagnostic=diagnostic(b);
    }
    return result;
}

std::vector<SmoD12SceneBody> step(std::span<const SmoD12SceneBody> input) {
    if(input.empty()||input.size()>16)throw std::invalid_argument("Cantidad de cuerpos duodecimales fuera del presupuesto.");
    std::vector<SmoD12SceneBody> integrated(input.begin(),input.end());
    const auto gravity=smo_d12_negate(literal(0x1,0x6,1));
    for(auto& b:integrated) {
        b.velocity[1]=smo_d12_add(b.velocity[1],smo_d12_mul(gravity,dt()));
        b.position=add(b.position,scale(b.velocity,dt()));b.contacts=0;b.diagnostic=diagnostic(b);
    }
    auto result=integrated;
    for(std::size_t index=0;index<input.size();++index) {
        auto& b=result[index];auto correction=zero3();b.contacts=0;
        for(std::size_t other=0;other<input.size();++other) {
            if(other==index)continue;const auto& c=integrated[other];const auto invalid=diagnostic(c)&smo_d12_fatal_mask;
            if(invalid) {b.diagnostic|=invalid;continue;}
            const auto delta=subtract(integrated[index].position,c.position);
            const auto distance=norm3(delta),radius=smo_d12_add(b.radius,c.radius);
            if(!smo_d12_valid(distance)||!smo_d12_valid(radius)) {b.diagnostic|=distance.diagnostic|radius.diagnostic;continue;}
            if(smo_d12_compare(distance,radius)>=0)continue;
            Vector normal;
            if(smo_d12_compare(distance,epsilon())>0)normal=divide(delta,distance);
            else {auto axis=one();if(index<other)axis=smo_d12_negate(axis);normal={axis,smo_d12_zero(),smo_d12_zero()};}
            const auto penetration=smo_d12_sub(radius,distance),gain=literal(0,0x3,1);
            correction=add(correction,scale(normal,smo_d12_mul(penetration,gain)));++b.contacts;
        }
        const auto length=norm3(correction),limit=literal(0,0x1,1);
        if(smo_d12_compare(length,limit)>0)correction=scale(correction,smo_d12_mul(limit,smo_d12_pinv(length,smo_d12_uint(3))));
        b.position=add(b.position,correction);b.velocity=add(b.velocity,divide(correction,dt()));
        for(auto& component:b.velocity)component=smo_d12_clamp(component,smo_d12_negate(smo_d12_uint(4)),smo_d12_uint(4));
        const auto restitution=literal(0,0x8,1),floor=smo_d12_add(smo_d12_negate(smo_d12_uint(2)),b.radius),
            top=smo_d12_sub(smo_d12_uint(2),b.radius),side=smo_d12_sub(smo_d12_uint(3),b.radius);
        if(smo_d12_compare(b.position[1],floor)<0) {b.position[1]=floor;b.velocity[1]=smo_d12_mul(smo_d12_abs(b.velocity[1]),restitution);++b.contacts;}
        if(smo_d12_compare(b.position[1],top)>0) {b.position[1]=top;b.velocity[1]=smo_d12_negate(smo_d12_mul(smo_d12_abs(b.velocity[1]),restitution));}
        if(smo_d12_compare(b.position[0],smo_d12_negate(side))<0) {b.position[0]=smo_d12_negate(side);b.velocity[0]=smo_d12_mul(smo_d12_abs(b.velocity[0]),restitution);}
        if(smo_d12_compare(b.position[0],side)>0) {b.position[0]=side;b.velocity[0]=smo_d12_negate(smo_d12_mul(smo_d12_abs(b.velocity[0]),restitution));}
        const auto near_z=smo_d12_add(one(),b.radius),far_z=smo_d12_sub(smo_d12_uint(4),b.radius);
        if(smo_d12_compare(b.position[2],near_z)<0) {b.position[2]=near_z;b.velocity[2]=smo_d12_mul(smo_d12_abs(b.velocity[2]),restitution);}
        if(smo_d12_compare(b.position[2],far_z)>0) {b.position[2]=far_z;b.velocity[2]=smo_d12_negate(smo_d12_mul(smo_d12_abs(b.velocity[2]),restitution));}
        b.diagnostic|=diagnostic(correction)|length.diagnostic;b.diagnostic=diagnostic(b);b.reserved={};
    }
    return result;
}
struct Hit {SmoD12 distance;bool hit=false;std::uint32_t diagnostic=0;};
Hit intersection(Vector origin,Vector positive_direction,const SmoD12SceneBody& b) {
    Hit result;result.diagnostic=diagnostic(b);
    if(result.diagnostic&smo_d12_fatal_mask)return result;
    const auto delta=subtract(origin,b.position);
    const auto a=magnitude2(positive_direction),h=dot_positive(delta,positive_direction),
        c=smo_d12_sub(magnitude2(delta),square(b.radius));
    // Match the selected GPU profile: fixed D12 uses widened composition;
    // F12 uses its explicit scalar-RNE tree with internal exponent range.
    // Both are independently implemented by the CPU integer128 core.
    const auto discriminant=smo_d12_discriminant(h,a,c);
    result.diagnostic|=a.diagnostic|h.diagnostic|c.diagnostic|discriminant.diagnostic;
    if(!smo_d12_valid(discriminant)||!smo_d12_valid(a))return result;
    if(smo_d12_compare(a,smo_d12_zero())<=0) {result.diagnostic|=smo_d12_domain;return result;}
    if(smo_d12_compare(discriminant,smo_d12_zero())<0)return result;
    const auto root=smo_d12_sqrt(discriminant),inverse=smo_d12_pinv(a,smo_d12_uint(3));
    auto t=smo_d12_mul(smo_d12_sub(smo_d12_negate(h),root),inverse);
    if(smo_d12_compare(t,epsilon())<0)t=smo_d12_mul(smo_d12_add(smo_d12_negate(h),root),inverse);
    result.diagnostic|=root.diagnostic|inverse.diagnostic|t.diagnostic;
    if(smo_d12_valid(t)&&smo_d12_compare(t,epsilon())>=0) {result.distance=t;result.hit=true;}
    return result;
}
std::uint32_t channel(SmoD12 input,std::uint32_t& diagnostics) {
    const auto value=smo_d12_mul(smo_d12_clamp(input,smo_d12_zero(),one()),smo_d12_uint(255));
    diagnostics|=value.diagnostic;
    if(!smo_d12_valid(value))return 0;
    if constexpr(Floating)return smo_f12_round_uint(value,diagnostics);
    else {auto whole=value.digits[2];
    if(value.digits[1]>10368||(value.digits[1]==10368&&(value.digits[0]||(whole&1))))++whole;
    return std::min(whole,255u);}
}
void finish(SmoD12ScenePixel& pixel) {
    pixel.diagnostic|=diagnostic(pixel.color)|pixel.distance.diagnostic;
    pixel.bgra=0xffff00ff;
    if(!(pixel.diagnostic&smo_d12_fatal_mask)) {
        const auto red=channel(pixel.color[0],pixel.diagnostic),green=channel(pixel.color[1],pixel.diagnostic),blue=channel(pixel.color[2],pixel.diagnostic);
        if(!(pixel.diagnostic&smo_d12_fatal_mask))pixel.bgra=0xff000000|(red<<16)|(green<<8)|blue;
    }
}
SmoD12ScenePixel ray(std::span<const SmoD12SceneBody> bodies,std::uint32_t pixel,const SmoD12SceneWorkload& work) {
    validate(work);
    if(bodies.size()!=work.bodies||pixel>=work.width*work.height)throw std::invalid_argument("Rayo duodecimal fuera de la escena acotada.");
    const auto x=pixel%work.width,y=pixel/work.width;
    const auto xcenter=smo_d12_sub(smo_d12_uint(x*2+1),smo_d12_uint(work.width)),
        ycenter=smo_d12_sub(smo_d12_uint(work.height),smo_d12_uint(y*2+1));
    const auto ix=smo_d12_pinv(smo_d12_uint(work.width),smo_d12_uint(3)),iy=smo_d12_pinv(smo_d12_uint(work.height),smo_d12_uint(3));
    const Vector origin{smo_d12_mul(smo_d12_mul(xcenter,ix),smo_d12_uint(4)),
        smo_d12_mul(smo_d12_mul(ycenter,iy),smo_d12_uint(3)),smo_d12_negate(smo_d12_uint(6))};
    const Vector direction{smo_d12_zero(),smo_d12_zero(),one()};
    SmoD12ScenePixel result;result.distance=smo_d12_zero();
    result.color={literal(0,0x03,2),literal(0,0x06,2),literal(0,0x09,2)};
    result.diagnostic=diagnostic(origin);
    for(std::uint32_t candidate=0;candidate<work.bodies;++candidate) {
        ++result.tests;const auto test=intersection(origin,direction,bodies[candidate]);result.diagnostic|=test.diagnostic;
        if(result.diagnostic&smo_d12_fatal_mask)break;
        if(test.hit&&(result.hit==std::numeric_limits<std::uint32_t>::max()||smo_d12_compare(test.distance,result.distance)<0)) {
            result.distance=test.distance;result.hit=candidate;
        }
    }
    if(result.hit!=std::numeric_limits<std::uint32_t>::max()&&!(result.diagnostic&smo_d12_fatal_mask)) {
        const auto& b=bodies[result.hit];const auto at=add(origin,scale(direction,result.distance));
        const auto normal=divide(subtract(at,b.position),b.radius);
        const auto light_delta=subtract(Vector{smo_d12_uint(6),smo_d12_uint(6),smo_d12_uint(8)},at);
        const auto light_distance=norm3(light_delta);const auto light=divide(light_delta,light_distance);
        const auto shadow_origin=add(at,scale(normal,epsilon()));
        result.diagnostic|=diagnostic(at)|diagnostic(normal)|light_distance.diagnostic|diagnostic(light)|diagnostic(shadow_origin);
        if(!(result.diagnostic&smo_d12_fatal_mask))for(std::uint32_t candidate=0;candidate<work.bodies;++candidate) {
            if(candidate==result.hit)continue;++result.tests;
            const auto test=intersection(shadow_origin,light,bodies[candidate]);result.diagnostic|=test.diagnostic;
            if(result.diagnostic&smo_d12_fatal_mask)break;
            if(test.hit&&smo_d12_compare(test.distance,light_distance)<0) {result.shadow=1;break;}
        }
        const auto diffuse=smo_d12_clamp(dot_positive(normal,light),smo_d12_zero(),one());
        const auto attenuation=smo_d12_clamp(smo_d12_mul(smo_d12_pinv(light_distance,smo_d12_uint(4)),literal(0x100)),smo_d12_zero(),one());
        const auto direct=result.shadow?literal(0,0x2,1):literal(0,0x9,1);
        const auto lighting=smo_d12_add(literal(0,0x2,1),smo_d12_mul(smo_d12_mul(diffuse,attenuation),direct));
        const Vector pigment{smo_d12_add(literal(0,0x3,1),smo_d12_mul(smo_d12_uint(result.hit%3),literal(0,0x2,1))),
            smo_d12_add(literal(0,0x4,1),smo_d12_mul(smo_d12_uint(result.hit%4),literal(0,0x1,1))),
            smo_d12_add(literal(0,0x6,1),smo_d12_mul(smo_d12_uint(result.hit%2),literal(0,0x2,1)))};
        result.color=scale(pigment,lighting);
        result.diagnostic|=diffuse.diagnostic|attenuation.diagnostic|lighting.diagnostic|diagnostic(result.color);
    }
    finish(result);return result;
}
};
} // namespace

std::vector<SmoD12SceneBody> smo_duodecimal_scene_initial(const SmoD12SceneWorkload& work,bool floating12) {
    return floating12?SceneReference<true>{}.initial(work):SceneReference<false>{}.initial(work);
}
std::vector<SmoD12SceneBody> smo_duodecimal_scene_step_reference(std::span<const SmoD12SceneBody> input,bool floating12) {
    return floating12?SceneReference<true>{}.step(input):SceneReference<false>{}.step(input);
}
SmoD12ScenePixel smo_duodecimal_scene_ray_reference(std::span<const SmoD12SceneBody> bodies,std::uint32_t pixel,const SmoD12SceneWorkload& work,bool floating12) {
    return floating12?SceneReference<true>{}.ray(bodies,pixel,work):SceneReference<false>{}.ray(bodies,pixel,work);
}
} // namespace lgaed::presentation
