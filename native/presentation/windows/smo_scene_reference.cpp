#include "presentation/windows/smo_scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
using Vec=std::array<float,3>;
constexpr float dt=1.0f/120.0f,gravity=-1.6f,restitution=.65f;
constexpr float infinity=1e20f;
Vec add(Vec a,const Vec& b){for(std::size_t k=0;k<3;++k)a[k]+=b[k];return a;}
Vec subtract(Vec a,const Vec& b){for(std::size_t k=0;k<3;++k)a[k]-=b[k];return a;}
Vec scale(Vec a,float nonnegative){for(auto& value:a)value*=nonnegative;return a;}
Vec divide(Vec a,float positive){for(auto& value:a)value/=positive;return a;}
float magnitude2(Vec value){for(auto& component:value)component=std::abs(component);
    const float xy=value[0]*value[0]+value[1]*value[1];return xy+value[2]*value[2];}
float dot(const Vec& value,const Vec& positive){const float xy=value[0]*positive[0]+value[1]*positive[1];return xy+value[2]*positive[2];}
void validate(std::span<const SmoSceneBody> bodies){
    if(bodies.size()>2048)throw std::invalid_argument("La escena de referencia admite como máximo 2048 cuerpos.");
    for(const auto& body:bodies){if(!std::isfinite(body.radius)||body.radius<=0)throw std::invalid_argument("Radio inválido en el oráculo FP32.");
        for(std::size_t k=0;k<3;++k)if(!std::isfinite(body.position[k])||!std::isfinite(body.velocity[k]))
            throw std::invalid_argument("Estado no finito en el oráculo FP32.");}
}
float intersection(const Vec& origin,const Vec& direction,const SmoSceneBody& body){
    const auto delta=subtract(origin,body.position);const float a=magnitude2(direction),h=dot(delta,direction),c=magnitude2(delta)-body.radius*body.radius;
    const float discriminant=std::abs(h)*std::abs(h)-a*c;
    if(discriminant<0)return infinity;
    const float root=std::sqrt(discriminant);float distance=(-h-root)/a;
    if(distance<.001f)distance=(-h+root)/a;return distance>=.001f?distance:infinity;
}
}

std::vector<SmoSceneBody> smo_scene_initial(std::uint32_t count){
    if(count>2048)throw std::invalid_argument("La escena inicial admite como máximo 2048 cuerpos.");
    std::vector<SmoSceneBody> result(count);
    for(std::uint32_t i=0;i<count;++i){auto& body=result[i];
        body.position={(static_cast<float>(i%32)-15.5f)*.27f,static_cast<float>((i/32)%8)*.29f+.1f,
            2.0f+static_cast<float>(i/(32*8))*.28f};
        body.radius=.18f+static_cast<float>(i%3)*.005f;
        body.velocity={(static_cast<float>(i%7)-3.0f)*.02f,0,(static_cast<float>(i%5)-2.0f)*.02f};body.contacts=0;
    }
    return result;
}

std::vector<SmoSceneBody> smo_scene_step_reference(std::span<const SmoSceneBody> input,std::uint32_t frame){
    validate(input);std::vector<SmoSceneBody> integrated(input.begin(),input.end());
    // This is an independent ordinary FP32 oracle of the shader's explicit
    // magnitude/positive-coefficient domain, not a generic SMO implementation.
    const float time=static_cast<float>(frame)*.0078125f,force=.12f*dt;
    for(std::size_t i=0;i<integrated.size();++i){auto& body=integrated[i];
        body.velocity[0]+=std::sin(time+static_cast<float>(i%17)*.125f)*force;
        body.velocity[2]+=std::cos(time+static_cast<float>(i%13)*.125f)*force;
        body.velocity[1]+=gravity*dt;body.position=add(body.position,scale(body.velocity,dt));body.contacts=0;
    }
    auto result=integrated;
    // Jacobi: every correction reads the same integrated state. Never update
    // a neighbour in place or change the shader's ascending candidate order.
    for(std::size_t i=0;i<result.size();++i){auto& body=result[i];Vec correction{};float contacts=0;
        for(std::size_t j=0;j<integrated.size();++j){if(i==j)continue;const auto& other=integrated[j];
            const auto delta=subtract(body.position,other.position);const float d2=magnitude2(delta),radius=body.radius+other.radius;
            if(d2>=radius*radius)continue;
            const float distance=std::sqrt(std::max(0.0f,d2));const auto normal=distance>.00001f?divide(delta,distance):Vec{i<j?-1.0f:1.0f,0,0};
            correction=add(correction,scale(normal,(radius-distance)*.25f));contacts+=1;
        }
        const float length=std::sqrt(magnitude2(correction));if(length>.08f)correction=scale(correction,.08f/length);
        body.position=add(body.position,correction);body.velocity=add(body.velocity,divide(correction,dt));
        for(auto& value:body.velocity)value=std::clamp(value,-4.0f,4.0f);
        const float floor=-3.0f+body.radius,top=3.0f-body.radius,side=4.8f-body.radius;
        if(body.position[1]<floor){body.position[1]=floor;body.velocity[1]=std::abs(body.velocity[1])*restitution;contacts+=1;}
        if(body.position[1]>top){body.position[1]=top;body.velocity[1]=-std::abs(body.velocity[1])*restitution;}
        if(body.position[0]<-side){body.position[0]=-side;body.velocity[0]=std::abs(body.velocity[0])*restitution;}
        if(body.position[0]>side){body.position[0]=side;body.velocity[0]=-std::abs(body.velocity[0])*restitution;}
        if(body.position[2]<.5f+body.radius){body.position[2]=.5f+body.radius;body.velocity[2]=std::abs(body.velocity[2])*restitution;}
        if(body.position[2]>4.8f-body.radius){body.position[2]=4.8f-body.radius;body.velocity[2]=-std::abs(body.velocity[2])*restitution;}
        body.contacts=contacts;
    }
    return result;
}

std::array<float,4> smo_scene_ray_reference(std::span<const SmoSceneBody> bodies,std::uint32_t pixel,const SmoSceneWorkload& workload){
    validate(bodies);const auto count=static_cast<std::uint64_t>(workload.width)*workload.height;
    if(workload.width==0||workload.height==0||count>std::numeric_limits<std::uint32_t>::max()||pixel>=count||bodies.size()!=workload.bodies)
        throw std::invalid_argument("Píxel o carga inválidos en el oráculo de rayos FP32.");
    const auto x=pixel%workload.width,y=pixel/workload.width;
    const Vec origin{(static_cast<float>(x)+.5f)/static_cast<float>(workload.width)*11.0f-5.5f,
        3.3f-(static_cast<float>(y)+.5f)/static_cast<float>(workload.height)*6.6f,-6.0f},direction{0,0,1};
    float nearest=infinity;std::size_t hit=bodies.size();
    for(std::size_t i=0;i<bodies.size();++i){const auto distance=intersection(origin,direction,bodies[i]);if(distance<nearest){nearest=distance;hit=i;}}
    Vec color{.025f,.04f,.07f};if(hit==bodies.size())return {color[0],color[1],color[2],-1};
    const auto& body=bodies[hit];const auto at=add(origin,scale(direction,nearest)),normal=divide(subtract(at,body.position),body.radius);
    const Vec light{.4082483f,.8164966f,.4082483f};const auto shadow_origin=add(at,scale(normal,.003f));bool shadow=false;
    for(std::size_t i=0;i<bodies.size();++i){if(i==hit)continue;if(intersection(shadow_origin,light,bodies[i])<100.0f){shadow=true;break;}}
    const float lighting=.35f+std::max(0.0f,dot(normal,light))*(shadow?.15f:.65f);
    const Vec pigment{.25f+static_cast<float>(hit%13)*.055f,.3f+static_cast<float>(hit%7)*.08f,.4f+static_cast<float>(hit%5)*.1f};
    color=scale(pigment,lighting);return {color[0],color[1],color[2],nearest};
}
}
