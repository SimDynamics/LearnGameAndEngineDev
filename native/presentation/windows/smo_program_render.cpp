#include "presentation/windows/smo_program_render.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace lgaed::presentation { namespace {
using V=SmoProgramRenderVector;
constexpr std::uint32_t background=0xff101820u;
void validate_size(std::uint32_t width,std::uint32_t height){if(!width||!height||width>512||height>288)throw std::invalid_argument("La vista F12 admite resoluciones de1..512 por1..288 píxeles.");}
std::uint32_t diagnostic(SmoF12 value){return smo_f12_abs(value).diagnostic;}
std::uint32_t diagnostic(const V& v){return diagnostic(v[0])|diagnostic(v[1])|diagnostic(v[2]);}
bool empty(SmoF12 value){return (value.digits[0]|value.digits[1]|value.digits[2])==0;}
SmoF12 failure(std::uint32_t bits){auto value=smo_f12_zero();value.diagnostic=bits;return value;}
SmoF12 product(SmoF12 a,SmoF12 b){auto value=smo_f12_mul(smo_f12_abs(a),smo_f12_abs(b));if(smo_f12_valid(value)&&!empty(value))value.negative=a.negative^b.negative;return value;}
SmoF12 quotient(SmoF12 a,SmoF12 b){const auto bits=diagnostic(a)|diagnostic(b);auto value=failure(bits|smo_d12_domain);
    if(!(bits&smo_d12_fatal_mask)&&!empty(b)){value=smo_f12_div(smo_f12_abs(a),smo_f12_abs(b));if(smo_f12_valid(value)&&!empty(value))value.negative=a.negative^b.negative;}return value;}
V subtract(const V& a,const V& b){return {smo_f12_sub(a[0],b[0]),smo_f12_sub(a[1],b[1]),smo_f12_sub(a[2],b[2])};}
V negative(const V& a){return {smo_f12_negate(a[0]),smo_f12_negate(a[1]),smo_f12_negate(a[2])};}
SmoF12 dot(const V& a,const V& b){return smo_f12_add(smo_f12_add(product(a[0],b[0]),product(a[1],b[1])),product(a[2],b[2]));}
V cross(const V& a,const V& b){return {smo_f12_sub(product(a[1],b[2]),product(a[2],b[1])),
    smo_f12_sub(product(a[2],b[0]),product(a[0],b[2])),smo_f12_sub(product(a[0],b[1]),product(a[1],b[0]))};}
std::uint32_t floor_count(SmoF12 value,std::uint32_t& bits){bits|=diagnostic(value);std::uint32_t result=0;
    if(smo_f12_valid(value)&&!value.negative&&smo_f12_compare(value,smo_f12_uint(1000000))<=0){auto coefficient=smo_f12_coefficient(value);auto exponent=smo_f12_exponent(value);
        while(exponent<0){coefficient/=12;++exponent;}while(exponent>0){coefficient*=12;--exponent;}result=static_cast<std::uint32_t>(coefficient);
    }else bits|=smo_d12_domain;return result;}
std::uint32_t color_word(const V& color,std::uint32_t& bits){std::array<std::uint32_t,3> channels{};for(unsigned i=0;i<3;++i){auto value=smo_f12_clamp(color[i],smo_f12_zero(),smo_f12_uint(1));
        value=smo_f12_mul(value,smo_f12_uint(255));bits|=value.diagnostic;channels[i]=smo_f12_round_uint(value,bits);}return 0xff000000u|(channels[0]<<16)|(channels[1]<<8)|channels[2];}
void finish_image(SmoProgramImage& image){image.pixel_hash=14695981039346656037ull;for(const auto& record:image.exact_pixels){image.diagnostic|=record.diagnostic;image.pixels.push_back(record.bgra);
        if(record.face!=0xffffffffu&&!(record.diagnostic&smo_d12_fatal_mask))++image.colored_pixels;
        for(unsigned byte=0;byte<4;++byte){image.pixel_hash^=(record.bgra>>(byte*8))&255u;image.pixel_hash*=1099511628211ull;}}}
struct Hit {SmoF12 depth=smo_f12_zero();bool hit=false;std::uint32_t diagnostic=0;};
Hit triangle(const V& direction,const V& a,const V& b,const V& c){Hit result;result.diagnostic=diagnostic(a)|diagnostic(b)|diagnostic(c)|diagnostic(direction);
    const auto e1=subtract(b,a),e2=subtract(c,a),p=cross(direction,e2);const auto determinant=dot(e1,p);result.diagnostic|=determinant.diagnostic;
    if(!(result.diagnostic&smo_d12_fatal_mask)&&!empty(determinant)){const auto inverse=quotient(smo_f12_uint(1),determinant);const auto t=negative(a);const auto u=product(dot(t,p),inverse);result.diagnostic|=inverse.diagnostic|u.diagnostic;
        if(smo_f12_valid(u)&&smo_f12_compare(u,smo_f12_zero())>=0&&smo_f12_compare(u,smo_f12_uint(1))<=0){const auto q=cross(t,e1);const auto v=product(dot(direction,q),inverse),sum=smo_f12_add(u,v);result.diagnostic|=v.diagnostic|sum.diagnostic;
            if(smo_f12_valid(v)&&smo_f12_compare(v,smo_f12_zero())>=0&&smo_f12_compare(sum,smo_f12_uint(1))<=0){const auto depth=product(dot(e2,q),inverse);result.diagnostic|=depth.diagnostic;
                if(smo_f12_valid(depth)&&smo_f12_compare(depth,smo_f12_literal(0,1,1))>=0&&smo_f12_compare(depth,smo_f12_uint(64))<=0){result.depth=depth;result.hit=true;}}}}
    return result;}
}

SmoProgramSinCos smo_program_sincos_reference(SmoF12 angle){SmoProgramSinCos result{smo_f12_zero(),smo_f12_uint(1),diagnostic(angle)};
    if(!(result.diagnostic&smo_d12_fatal_mask)&&!empty(angle)){const auto absolute=smo_f12_abs(angle);result.diagnostic|=smo_d12_inexact;
        if(smo_f12_compare(absolute,smo_f12_uint(1000000))>0)result.diagnostic|=smo_d12_domain;
        else if(smo_f12_compare(absolute,smo_f12_literal(0,1,6))<=0)result.sine=angle;
        else{auto pi=smo_f12_parse("3.184809493~9");pi.diagnostic|=smo_d12_inexact;const auto two_pi=smo_f12_add(pi,pi),half_pi=quotient(pi,smo_f12_uint(2));auto x=absolute;
            if(smo_f12_compare(x,pi)>0){const auto cycles=quotient(x,two_pi);const auto whole=floor_count(cycles,result.diagnostic);x=smo_f12_sub(x,product(two_pi,smo_f12_uint(whole)));if(smo_f12_compare(x,pi)>0)x=smo_f12_sub(x,two_pi);}
            if(angle.negative)x=smo_f12_negate(x);bool negative_cosine=false;
            if(smo_f12_compare(x,half_pi)>0){x=smo_f12_sub(pi,x);negative_cosine=true;}
            else if(smo_f12_compare(x,smo_f12_negate(half_pi))<0){x=smo_f12_sub(smo_f12_negate(pi),x);negative_cosine=true;}
            result.diagnostic|=x.diagnostic|half_pi.diagnostic;
            if(!(result.diagnostic&smo_d12_fatal_mask)){const auto squared=product(x,x);auto sine=x,cosine=smo_f12_uint(1),sine_term=x,cosine_term=cosine;
                for(unsigned n=1;n<=12;++n){const auto old_sine=sine,old_cosine=cosine;
                    sine_term=smo_f12_negate(quotient(product(sine_term,squared),smo_f12_uint((2*n)*(2*n+1))));
                    cosine_term=smo_f12_negate(quotient(product(cosine_term,squared),smo_f12_uint((2*n-1)*(2*n))));
                    sine=smo_f12_add(sine,sine_term);cosine=smo_f12_add(cosine,cosine_term);result.diagnostic|=sine.diagnostic|cosine.diagnostic|sine_term.diagnostic|cosine_term.diagnostic;
                    if(smo_f12_compare(old_sine,sine)==0&&smo_f12_compare(old_cosine,cosine)==0)break;}
                result.sine=sine;if(negative_cosine)cosine=smo_f12_negate(cosine);result.cosine=cosine;}
        }}result.sine.diagnostic|=result.diagnostic;result.cosine.diagnostic|=result.diagnostic;return result;}

SmoProgramImage smo_program_render_reference(std::span<const V> vertices,std::span<const std::array<std::uint32_t,3>> faces,const V& rotation,const V& color,
    std::uint32_t width,std::uint32_t height,bool draw_requested,std::uint32_t bits){validate_size(width,height);if(vertices.size()>1024||faces.size()>2048)throw std::invalid_argument("La geometría F12 supera1024 vértices/2048 caras.");
    std::array<SmoProgramSinCos,3> trigs;for(unsigned i=0;i<3;++i){trigs[i]=smo_program_sincos_reference(rotation[i]);bits|=trigs[i].diagnostic;}const auto bgra=color_word(color,bits);std::vector<V> transformed;
    const auto setup_bits=bits;
    for(auto p:vertices){auto d=diagnostic(p)|setup_bits;const auto bound=smo_f12_uint(1000000);if(!smo_f12_valid(p[0])||!smo_f12_valid(p[1])||!smo_f12_valid(p[2])||
            smo_f12_compare(smo_f12_abs(p[0]),bound)>0||smo_f12_compare(smo_f12_abs(p[1]),bound)>0||smo_f12_compare(smo_f12_abs(p[2]),bound)>0)d|=smo_d12_domain;
        if(!(d&smo_d12_fatal_mask)){const auto sx=trigs[0].sine,cx=trigs[0].cosine,sy=trigs[1].sine,cy=trigs[1].cosine,sz=trigs[2].sine,cz=trigs[2].cosine;
            auto y=smo_f12_sub(product(p[1],cx),product(p[2],sx)),z=smo_f12_add(product(p[1],sx),product(p[2],cx));p[1]=y;p[2]=z;
            auto x=smo_f12_add(product(p[0],cy),product(p[2],sy));z=smo_f12_sub(product(p[2],cy),product(p[0],sy));p[0]=x;p[2]=z;
            x=smo_f12_sub(product(p[0],cz),product(p[1],sz));y=smo_f12_add(product(p[0],sz),product(p[1],cz));p[0]=x;p[1]=y;p[2]=smo_f12_add(p[2],smo_f12_uint(4));d|=diagnostic(p);}
        bits|=d;transformed.push_back(p);}
    for(const auto& face:faces)if(face[0]>=vertices.size()||face[1]>=vertices.size()||face[2]>=vertices.size())bits|=smo_d12_encoding;
    SmoProgramImage image;image.width=width;image.height=height;image.diagnostic=bits;image.exact_pixels.reserve(static_cast<std::size_t>(width)*height);
    const auto half=smo_f12_literal(0,6,1),center_x=quotient(smo_f12_uint(width),smo_f12_uint(2)),center_y=quotient(smo_f12_uint(height),smo_f12_uint(2));const auto focal=smo_f12_mul(smo_f12_uint(std::min(width,height)),smo_f12_literal(1,6,1));
    for(std::uint32_t pixel=0;pixel<width*height;++pixel){SmoProgramRenderPixel record;record.depth=smo_f12_zero();record.bgra=background;record.diagnostic=bits;
        if(draw_requested&&!(bits&smo_d12_fatal_mask)){const auto x=pixel%width,y=pixel/width;V direction{quotient(smo_f12_sub(smo_f12_add(smo_f12_uint(x),half),center_x),focal),
                quotient(smo_f12_sub(center_y,smo_f12_add(smo_f12_uint(y),half)),focal),smo_f12_uint(1)};record.diagnostic|=diagnostic(direction);
            for(std::uint32_t face=0;face<faces.size();++face){const auto& indices=faces[face];if(indices[0]<vertices.size()&&indices[1]<vertices.size()&&indices[2]<vertices.size()){
                    const auto hit=triangle(direction,transformed[indices[0]],transformed[indices[1]],transformed[indices[2]]);record.diagnostic|=hit.diagnostic;
                    if(hit.hit&&(record.face==0xffffffffu||smo_f12_compare(hit.depth,record.depth)<0)){record.face=face;record.depth=hit.depth;}}
                else record.diagnostic|=smo_d12_encoding;}
            if(record.face!=0xffffffffu)record.bgra=bgra;}
        if(record.diagnostic&smo_d12_fatal_mask)record.bgra=0xffff00ffu;image.exact_pixels.push_back(record);}
    finish_image(image);return image;}

struct SmoProgramRenderer::Impl {
    SmoCompute& gpu;std::array<SmoKernel,smo_program_render_stages> kernels{};SmoBuffer transformed=0,scratch=0,pixels=0,face_cache=0;std::uint32_t width=0,height=0,capacity=0;bool uncached_loaded=false;
    Impl(SmoCompute& device,std::uint32_t w,std::uint32_t h):gpu(device){for(unsigned stage=0;stage<4u;++stage)kernels[stage]=gpu.kernel(smo_program_render_kernel(stage));
        transformed=gpu.buffer(1024u*smo_program_render_vertex_words*4u);scratch=gpu.buffer(40u*4u);face_cache=gpu.buffer(2048u*smo_program_render_face_words*4u);resize(w,h);}
    void resize(std::uint32_t w,std::uint32_t h){validate_size(w,h);const auto needed=w*h;if(needed>capacity){capacity=1;while(capacity<needed)capacity*=2;pixels=gpu.buffer(static_cast<std::size_t>(capacity)*sizeof(SmoProgramRenderPixel));}width=w;height=h;}
};
SmoProgramRenderer::SmoProgramRenderer(SmoCompute& gpu,std::uint32_t width,std::uint32_t height):impl_(std::make_unique<Impl>(gpu,width,height)){}
SmoProgramRenderer::~SmoProgramRenderer()=default;
SmoProgramRenderer::SmoProgramRenderer(SmoProgramRenderer&&) noexcept=default;
SmoProgramRenderer& SmoProgramRenderer::operator=(SmoProgramRenderer&&) noexcept=default;
void SmoProgramRenderer::resize(std::uint32_t width,std::uint32_t height){if(!impl_)throw std::logic_error("El renderer F12 ya fue movido.");impl_->resize(width,height);}
SmoProgramImage SmoProgramRenderer::render(const SmoGpuFrame& frame,bool capture_exact,bool timed,SmoProgramRenderMode mode){if(!impl_)throw std::logic_error("El renderer F12 ya fue movido.");auto& r=*impl_;
    if(!frame.vertices||!frame.faces||!frame.frame||frame.vertex_count>1024||frame.face_count>2048||
        frame.vertex_word_offset>std::numeric_limits<std::uint32_t>::max()-frame.vertex_count*18u||frame.face_word_offset>std::numeric_limits<std::uint32_t>::max()-frame.face_count*3u||frame.frame_word_offset>std::numeric_limits<std::uint32_t>::max()-40u)
        throw std::invalid_argument("La transacción GPU no contiene geometría F12 direccionable.");
    if(mode!=SmoProgramRenderMode::cached&&mode!=SmoProgramRenderMode::reference_uncached)throw std::invalid_argument("Modalidad gráfica F12 desconocida.");
    if(mode==SmoProgramRenderMode::reference_uncached&&!r.uncached_loaded){r.kernels[4]=r.gpu.kernel(smo_program_render_kernel(4));r.uncached_loaded=true;}
    const auto begin=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    std::array<std::uint32_t,16> parameters{r.width,r.height,frame.vertex_count,frame.face_count,frame.vertex_word_offset,frame.face_word_offset,frame.frame_word_offset,0};
    const std::array<SmoBuffer,8> buffers{frame.vertices,frame.faces,frame.frame,r.transformed,r.scratch,r.pixels,r.face_cache,0};std::array<SmoDispatch,5> calls;
    calls[0]={r.kernels[0],buffers,parameters,{1,1,1}};calls[1]={r.kernels[1],buffers,parameters,{std::max(1u,(frame.vertex_count+63u)/64u),1,1}};
    parameters[7]=1;calls[2]={r.kernels[0],buffers,parameters,{1,1,1}};parameters[7]=0;std::size_t passes=4;
    if(mode==SmoProgramRenderMode::cached){calls[3]={r.kernels[3],buffers,parameters,{std::max(1u,(frame.face_count+63u)/64u),1,1}};calls[4]={r.kernels[2],buffers,parameters,{(r.width*r.height+63u)/64u,1,1}};passes=5;}
    else calls[3]={r.kernels[4],buffers,parameters,{(r.width*r.height+63u)/64u,1,1}};
    const auto gpu_ms=r.gpu.execute(std::span(calls.data(),passes),timed);const auto read_begin=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const auto raw=r.gpu.read(r.pixels);const auto read_end=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    if(raw.size()!=static_cast<std::size_t>(r.capacity)*sizeof(SmoProgramRenderPixel))throw std::runtime_error("La lectura de imagen F12 está incompleta.");
    SmoProgramImage image;image.width=r.width;image.height=r.height;image.exact_pixels.resize(static_cast<std::size_t>(r.width)*r.height);
    std::memcpy(image.exact_pixels.data(),raw.data(),image.exact_pixels.size()*sizeof(SmoProgramRenderPixel));finish_image(image);if(!capture_exact)image.exact_pixels.clear();
    if(timed){image.gpu_ms=gpu_ms;image.readback_ms=std::chrono::duration<double,std::milli>(read_end-read_begin).count();image.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}return image;}
} //namespace lgaed::presentation
