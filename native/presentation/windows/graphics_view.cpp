#include "presentation/windows/graphics_view.hpp"
#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/ui_language.hpp"
#include "platform/windows/state_store.hpp"
#include "core/smo.hpp"
#include "../../../app/src/main/cpp/physics-scene.h"
#include <commctrl.h>
#include <windowsx.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace lgaed::presentation {
namespace {
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
constexpr wchar_t lab_class[]=L"LearnGameAndEngineDev.GraphicsLab";
constexpr int back_id=5201,title_id=5220,canvas_id=5221,status_id=5213,reset_id=5212;
constexpr int auto_id=5260,hardware_id=5261,warp_id=5262,simulation_id=5263,load_id=5264,instructions_id=5265;
constexpr UINT stats_message=WM_APP+249;
void require_hr(HRESULT result,const char* action){if(FAILED(result))throw std::runtime_error(std::string(action)+" (HRESULT "+std::to_string(static_cast<unsigned>(result))+")");}
struct Command {enum class Kind{down,move,up,cancel,reset};Kind kind;float u=0,v=0,vx=0,vy=0;};
struct Readback {bool pixels;std::promise<GraphicsLabSnapshot> reply;};
struct Parameters {float transform[4],material[4],frame[4];};
struct SmoVertex {float position[3],normal[3];};
constexpr unsigned sphere_vertices=16*12*6;

// The procedural mesh, lighting, floor and benchmark transform reproduce the
// original Android renderer. Only clip-space Z changes from GL to Direct3D.
constexpr char shader[]=R"(
cbuffer Object : register(b0) {float4 transform;float4 material;float4 frame;};
struct Pixel {float4 position:SV_POSITION;float3 normal:NORMAL;float3 base:COLOR;float3 world:TEXCOORD;nointerpolation uint kind:KIND;};
Pixel VSMain(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
    Pixel result;uint kind=(uint)(material.w+0.5);float3 normal,world;
    if(kind==1){const float2 corners[6]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,-1),float2(1,1),float2(-1,1)};
        float2 uv=corners[vertex];world=float3(uv.x*2.25,-1.90,-5.95+uv.y*2.35);normal=float3(0,1,0);
    }else{uint cell=vertex/6,corner=vertex%6;float u=(float)(cell%16+((corner==1||corner==2||corner==4)?1:0))/16.0;
        float v=(float)(cell/16+((corner==2||corner==4||corner==5)?1:0))/12.0;float longitude=u*6.28318530718,latitude=v*3.14159265359;
        normal=float3(sin(latitude)*cos(longitude),cos(latitude),sin(latitude)*sin(longitude));
        if(kind==3){uint side=max(1,(uint)(frame.z+0.5)),column=instance%side,row=instance/side;float phase=frame.y*0.85+(float)instance*0.157;
            float cp=cos(phase),sp=sin(phase);float3 rotated=normalize(float3(cp*normal.x+sp*normal.z,normal.y*cos(phase*0.47)-normal.z*sin(phase*0.47),-sp*normal.x+cp*normal.z));
            float gap=0.85/(float)side;float2 center=((float2((float)column,(float)row)+0.5)/(float)side*2-1)*0.94;
            result.position=float4(center+rotated.xy*gap,0.5-rotated.z*0.12,1);result.normal=rotated;
            result.base=0.28+0.62*float3(0.5+0.5*sin((float)instance*0.21),0.5+0.5*sin((float)instance*0.37+1.5),0.5+0.5*cos((float)instance*0.13+0.6));
            result.world=rotated;result.kind=3;return result;}
        world=transform.xyz+normal*transform.w;
    }
    result.position=float4(1.8*world.x/max(frame.x,0.2),1.8*world.y,-1.001001*world.z-0.1001001,-world.z);
    result.normal=normal;result.base=material.rgb;result.world=world;result.kind=kind;return result;
}
float4 PSMain(Pixel value):SV_TARGET{
    float diffuse=max(dot(normalize(value.normal),normalize(float3(-0.55,0.9,0.75))),0);float3 shaded=value.base*(0.28+0.72*diffuse);
    if(value.kind==1){float2 grid=abs(frac(value.world.xz*1.35)-0.5);float grid_line=1-smoothstep(0.465,0.495,max(grid.x,grid.y));shaded=lerp(float3(0.09,0.17,0.23),float3(0.14,0.23,0.31),grid_line);}
    else if(value.kind==2)shaded+=float3(0.25,0.22,0.06);return float4(shaded,1);
})";

// SMO owns the mesh, angles and draw request. This conventional projection,
// normal calculation and trigonometry belong only to the graphics bridge;
// they are not implementations or proofs of SMO's arithmetic laws.
constexpr char smo_shader[]=R"(
cbuffer Object : register(b0) {float4 angles;float4 material;float4 projection;};
struct Input {float3 position:POSITION;float3 normal:NORMAL;};
struct Pixel {float4 position:SV_POSITION;float3 normal:NORMAL;};
float3 rotate(float3 p) {
    float sx,cx,sy,cy,sz,cz;sincos(angles.x,sx,cx);sincos(angles.y,sy,cy);sincos(angles.z,sz,cz);
    p=float3(p.x,cx*p.y-sx*p.z,sx*p.y+cx*p.z);
    p=float3(cy*p.x+sy*p.z,p.y,-sy*p.x+cy*p.z);
    return float3(cz*p.x-sz*p.y,sz*p.x+cz*p.y,p.z);
}
Pixel VSMain(Input input) {
    Pixel result;float3 p=rotate(input.position);p.z-=5;
    result.position=float4(1.8*p.x/max(projection.x,0.2),1.8*p.y,-1.001001*p.z-0.1001001,-p.z);
    result.normal=rotate(input.normal);return result;
}
float4 PSMain(Pixel value):SV_TARGET {
    float light=0.35+0.65*abs(dot(normalize(value.normal),normalize(float3(-0.55,0.9,0.75))));
    return float4(material.rgb*light,1);
})";

struct Gpu {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11DepthStencilState> depth_state;
    ComPtr<ID3D11VertexShader> smo_vs;ComPtr<ID3D11PixelShader> smo_ps;
    ComPtr<ID3D11InputLayout> smo_layout;ComPtr<ID3D11Buffer> smo_vertices;
    unsigned smo_capacity=0;
    unsigned width=0,height=0;bool software=false;std::string description;
    ~Gpu(){if(context){context->ClearState();context->Flush();}}
    void initialize(HWND surface,GraphicsBackend preference,bool sphere_pipeline=true){
        DXGI_SWAP_CHAIN_DESC swap{};swap.BufferDesc.Width=1;swap.BufferDesc.Height=1;swap.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        swap.SampleDesc.Count=1;swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swap.BufferCount=2;swap.OutputWindow=surface;swap.Windowed=TRUE;swap.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};D3D_FEATURE_LEVEL level{};
        const auto create=[&](D3D_DRIVER_TYPE type){return D3D11CreateDeviceAndSwapChain(nullptr,type,nullptr,0,levels,3,D3D11_SDK_VERSION,&swap,chain.ReleaseAndGetAddressOf(),device.ReleaseAndGetAddressOf(),&level,context.ReleaseAndGetAddressOf());};
        software=preference==GraphicsBackend::warp;auto result=create(software?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE);
        if(FAILED(result)&&preference==GraphicsBackend::automatic){software=true;result=create(D3D_DRIVER_TYPE_WARP);}require_hr(result,"Create Direct3D 11 device");
        ComPtr<IDXGIDevice> dxgi;require_hr(device.As(&dxgi),"Read DXGI device");ComPtr<IDXGIAdapter> adapter;require_hr(dxgi->GetAdapter(adapter.GetAddressOf()),"Read graphics adapter");DXGI_ADAPTER_DESC info{};require_hr(adapter->GetDesc(&info),"Read graphics adapter name");
        description="Direct3D 11 · "+utf8(info.Description)+(software?" · WARP (software)":" · hardware");
        if(sphere_pipeline){ComPtr<ID3DBlob> vertex,pixel,errors;auto compiled=D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"VSMain","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,vertex.GetAddressOf(),errors.GetAddressOf());
        if(FAILED(compiled)&&errors)throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));require_hr(compiled,"Compile original sphere vertex shader");
        compiled=D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"PSMain","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,pixel.GetAddressOf(),errors.ReleaseAndGetAddressOf());
        if(FAILED(compiled)&&errors)throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));require_hr(compiled,"Compile original sphere pixel shader");
        require_hr(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,vs.GetAddressOf()),"Create sphere vertex shader");require_hr(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,ps.GetAddressOf()),"Create sphere pixel shader");}
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(Parameters);buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;require_hr(device->CreateBuffer(&buffer,nullptr,constants.GetAddressOf()),"Create sphere parameters");
        D3D11_RASTERIZER_DESC state{};state.FillMode=D3D11_FILL_SOLID;state.CullMode=D3D11_CULL_NONE;state.DepthClipEnable=TRUE;require_hr(device->CreateRasterizerState(&state,raster.GetAddressOf()),"Create sphere rasterizer");
        D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=TRUE;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;z.DepthFunc=D3D11_COMPARISON_LESS;require_hr(device->CreateDepthStencilState(&z,depth_state.GetAddressOf()),"Create sphere depth test");
    }
    void resize(unsigned x,unsigned y){
        x=std::max(1u,x);y=std::max(1u,y);if(width==x&&height==y)return;
        context->OMSetRenderTargets(0,nullptr,nullptr);target.Reset();depth.Reset();require_hr(chain->ResizeBuffers(0,x,y,DXGI_FORMAT_UNKNOWN,0),"Resize graphics surface");
        ComPtr<ID3D11Texture2D> pixels;require_hr(chain->GetBuffer(0,IID_PPV_ARGS(pixels.GetAddressOf())),"Get actual graphics buffer");require_hr(device->CreateRenderTargetView(pixels.Get(),nullptr,target.GetAddressOf()),"Create graphics surface");
        D3D11_TEXTURE2D_DESC z{};z.Width=x;z.Height=y;z.MipLevels=1;z.ArraySize=1;z.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;z.SampleDesc.Count=1;z.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> texture;require_hr(device->CreateTexture2D(&z,nullptr,texture.GetAddressOf()),"Create graphics depth buffer");require_hr(device->CreateDepthStencilView(texture.Get(),nullptr,depth.GetAddressOf()),"Create graphics depth surface");width=x;height=y;
    }
    void object(const Parameters& value,unsigned vertices,unsigned instances=1){context->UpdateSubresource(constants.Get(),0,nullptr,&value,0,0);if(instances==1)context->Draw(vertices,0);else context->DrawInstanced(vertices,instances,0,0);}
    void draw(const lgp::PhysicsScene& scene,bool benchmark,unsigned side,float seconds,bool present){
        constexpr float blue[]{0.055f,0.105f,0.19f,1};context->ClearRenderTargetView(target.Get(),blue);context->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,1,0);
        auto* color=target.Get();context->OMSetRenderTargets(1,&color,depth.Get());context->OMSetDepthStencilState(depth_state.Get(),0);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);auto* buffer=constants.Get();context->VSSetConstantBuffers(0,1,&buffer);context->PSSetConstantBuffers(0,1,&buffer);
        Parameters parameters{{0,0,0,1},{0.14f,0.23f,0.31f,1},{static_cast<float>(width)/height,seconds,static_cast<float>(side),0}};
        if(benchmark){parameters.material[3]=3;object(parameters,sphere_vertices,side*side);}else{
            object(parameters,6);const auto& bodies=scene.get_bodies();for(std::size_t index=0;index<bodies.size();++index){const auto& body=bodies[index];
                parameters.transform[0]=body.position.x;parameters.transform[1]=body.position.y;parameters.transform[2]=body.position.z;parameters.transform[3]=body.radius;
                parameters.material[0]=body.color.x;parameters.material[1]=body.color.y;parameters.material[2]=body.color.z;parameters.material[3]=scene.selected_index()==static_cast<int>(index)?2.0f:0.0f;object(parameters,sphere_vertices);}}
        if(present)require_hr(chain->Present(1,0),"Present graphics frame");
    }
    void prepare_smo(){
        if(smo_vs)return;
        ComPtr<ID3DBlob> vertex,pixel,errors;
        const auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& code){
            const auto result=D3DCompile(smo_shader,sizeof(smo_shader),nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,code.GetAddressOf(),errors.ReleaseAndGetAddressOf());
            if(FAILED(result)&&errors)throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
            require_hr(result,"Compile SMO graphics bridge shader");
        };
        compile("VSMain","vs_4_0",vertex);compile("PSMain","ps_4_0",pixel);
        ComPtr<ID3D11VertexShader> vertex_shader;ComPtr<ID3D11PixelShader> pixel_shader;ComPtr<ID3D11InputLayout> layout;
        require_hr(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,vertex_shader.GetAddressOf()),"Create SMO mesh vertex shader");
        require_hr(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,pixel_shader.GetAddressOf()),"Create SMO mesh pixel shader");
        constexpr D3D11_INPUT_ELEMENT_DESC input[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
        require_hr(device->CreateInputLayout(input,2,vertex->GetBufferPointer(),vertex->GetBufferSize(),layout.GetAddressOf()),"Create SMO mesh vertex layout");
        smo_vs=std::move(vertex_shader);smo_ps=std::move(pixel_shader);smo_layout=std::move(layout);
    }
    void draw_smo(const smo::Frame& frame,bool present){
        if(frame.vertices.size()>65536||frame.faces.size()>32768)throw std::invalid_argument("SMO graphics mesh exceeds prototype budget");
        const auto coordinate=[](double value){
            if(!std::isfinite(value)||std::abs(value)>static_cast<double>(std::numeric_limits<float>::max())/8)
                throw std::invalid_argument("SMO coordinate cannot be represented by the graphics support");
            return static_cast<float>(value);
        };
        const auto angle=[](double value){if(!std::isfinite(value))throw std::invalid_argument("SMO rotation must be finite");
            return static_cast<float>(std::remainder(value,6.2831853071795864769));};
        const auto channel=[](double value){if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("SMO drawing color must be between zero and one");return static_cast<float>(value);};
        Parameters parameters{{angle(frame.rotation.x),angle(frame.rotation.y),angle(frame.rotation.z),0},
            {channel(frame.color.x),channel(frame.color.y),channel(frame.color.z),1},{static_cast<float>(width)/height,0,0,0}};
        std::vector<SmoVertex> vertices;
        if(frame.draw_requested){
            vertices.reserve(frame.faces.size()*3);
            for(const auto& face:frame.faces){
                for(const auto index:face)if(index>=frame.vertices.size())throw std::invalid_argument("SMO face references a missing vertex");
                const auto& a=frame.vertices[face[0]];const auto& b=frame.vertices[face[1]];const auto& c=frame.vertices[face[2]];
                // Geometric normals are adapter calculations, not SMO products.
                const double ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
                const double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
                const double magnitude=std::hypot(nx,ny,nz);
                if(!std::isfinite(magnitude))throw std::invalid_argument("SMO face normal exceeds the graphics support range");
                const float normal[]{magnitude>0?static_cast<float>(nx/magnitude):0,
                    magnitude>0?static_cast<float>(ny/magnitude):0,magnitude>0?static_cast<float>(nz/magnitude):1};
                for(const auto index:face){const auto& point=frame.vertices[index];vertices.push_back({{coordinate(point.x),coordinate(point.y),coordinate(point.z)},{normal[0],normal[1],normal[2]}});}
            }
        }
        constexpr float blue[]{0.055f,0.105f,0.19f,1};context->ClearRenderTargetView(target.Get(),blue);context->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,1,0);
        auto* color=target.Get();context->OMSetRenderTargets(1,&color,depth.Get());context->OMSetDepthStencilState(depth_state.Get(),0);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());
        if(!vertices.empty()){
            prepare_smo();const auto count=static_cast<unsigned>(vertices.size());
            if(count>smo_capacity){D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=static_cast<UINT>(count*sizeof(SmoVertex));buffer.Usage=D3D11_USAGE_DYNAMIC;
                buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
                require_hr(device->CreateBuffer(&buffer,nullptr,smo_vertices.ReleaseAndGetAddressOf()),"Create SMO mesh vertex buffer");smo_capacity=count;}
            D3D11_MAPPED_SUBRESOURCE mapped{};require_hr(context->Map(smo_vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Upload SMO mesh vertices");
            std::memcpy(mapped.pData,vertices.data(),vertices.size()*sizeof(SmoVertex));context->Unmap(smo_vertices.Get(),0);
            auto* source=smo_vertices.Get();constexpr UINT stride=sizeof(SmoVertex),offset=0;
            context->IASetInputLayout(smo_layout.Get());context->IASetVertexBuffers(0,1,&source,&stride,&offset);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(smo_vs.Get(),nullptr,0);context->PSSetShader(smo_ps.Get(),nullptr,0);
            context->UpdateSubresource(constants.Get(),0,nullptr,&parameters,0,0);auto* buffer=constants.Get();context->VSSetConstantBuffers(0,1,&buffer);context->PSSetConstantBuffers(0,1,&buffer);
            context->Draw(count,0);
        }
        if(present)require_hr(chain->Present(1,0),"Present SMO graphics frame");
    }
    std::size_t read_pixels(std::uint64_t* hash=nullptr){
        ComPtr<ID3D11Texture2D> source;require_hr(chain->GetBuffer(0,IID_PPV_ARGS(source.GetAddressOf())),"Read actual graphics frame");D3D11_TEXTURE2D_DESC shape{};source->GetDesc(&shape);
        shape.Usage=D3D11_USAGE_STAGING;shape.BindFlags=0;shape.CPUAccessFlags=D3D11_CPU_ACCESS_READ;shape.MiscFlags=0;ComPtr<ID3D11Texture2D> copy;require_hr(device->CreateTexture2D(&shape,nullptr,copy.GetAddressOf()),"Create graphics readback");context->CopyResource(copy.Get(),source.Get());
        D3D11_MAPPED_SUBRESOURCE pixels{};require_hr(context->Map(copy.Get(),0,D3D11_MAP_READ,0,&pixels),"Read GPU pixels");std::size_t colored=0;
        for(unsigned row=0;row<height;++row){const auto* at=static_cast<const unsigned char*>(pixels.pData)+row*pixels.RowPitch;for(unsigned column=0;column<width;++column){const auto* pixel=at+column*4;if(pixel[0]>64||pixel[1]>64||pixel[2]>64)++colored;}}
        if(hash){std::uint64_t value=14695981039346656037ull;
            for(unsigned row=0;row<height;++row){const auto* at=static_cast<const unsigned char*>(pixels.pData)+row*pixels.RowPitch;
                for(unsigned column=0;column<width*4;++column){value^=at[column];value*=1099511628211ull;}}
            *hash=value;}
        context->Unmap(copy.Get(),0);return colored;
    }
};

constexpr wchar_t smo_surface_class[]=L"LearnGameAndEngineDev.SmoSurface";
struct SmoSurface {
    HWND window=nullptr;GraphicsBackend preference=GraphicsBackend::automatic;
    std::unique_ptr<Gpu> gpu;std::optional<smo::Frame> frame;
    std::uint64_t frames=0;std::string error;bool owned=false,drawing=false;
    explicit SmoSurface(GraphicsBackend requested):preference(requested){}
    void initialize(HWND target){
        window=target;auto created=std::make_unique<Gpu>();created->initialize(window,preference,false);
        RECT area{};GetClientRect(window,&area);created->resize(static_cast<unsigned>(std::max(1L,area.right)),static_cast<unsigned>(std::max(1L,area.bottom)));
        gpu=std::move(created);redraw();
    }
    void redraw(){
        if(!gpu||drawing)return;
        struct Drawing {bool& value;explicit Drawing(bool& field):value(field){value=true;}~Drawing(){value=false;}} guard(drawing);
        const auto show=IsWindowVisible(window)&&!IsIconic(GetAncestor(window,GA_ROOT));
        gpu->draw_smo(frame.value_or(smo::Frame{}),show);
    }
    void resize(){
        if(!gpu||drawing)return;
        RECT area{};GetClientRect(window,&area);
        gpu->resize(static_cast<unsigned>(std::max(1L,area.right)),static_cast<unsigned>(std::max(1L,area.bottom)));redraw();
    }
    void submit(const smo::Frame& next){
        if(!gpu)throw std::runtime_error("SMO graphics surface is unavailable");
        if(drawing)throw std::runtime_error("SMO graphics surface is already drawing");
        struct Drawing {bool& value;explicit Drawing(bool& field):value(field){value=true;}~Drawing(){value=false;}} guard(drawing);
        // A synchronous DXGI message may resize its HWND while presentation is
        // guarded. Reconcile the real client before the next submitted frame.
        RECT area{};GetClientRect(window,&area);gpu->resize(static_cast<unsigned>(std::max(1L,area.right)),static_cast<unsigned>(std::max(1L,area.bottom)));
        gpu->draw_smo(next,IsWindowVisible(window)&&!IsIconic(GetAncestor(window,GA_ROOT)));
        frame=next;++frames;error.clear();
    }
    SmoSurfaceSnapshot inspect(bool pixels){
        SmoSurfaceSnapshot value;value.frames=frames;value.error=error;
        if(gpu){value.width=gpu->width;value.height=gpu->height;value.backend=gpu->description;value.software=gpu->software;
            if(pixels){
                if(drawing)throw std::runtime_error("SMO graphics readback is already drawing");
                struct Drawing {bool& value;explicit Drawing(bool& field):value(field){value=true;}~Drawing(){value=false;}} guard(drawing);
                // DISCARD swap chains need a redraw before readback. Reuse the
                // exact last submitted SMO frame; never evaluate or advance it.
                gpu->draw_smo(frame.value_or(smo::Frame{}),false);
                value.colored_pixels=gpu->read_pixels(&value.pixel_hash);
            }}
        return value;
    }
};
SmoSurface* smo_surface(HWND window){return reinterpret_cast<SmoSurface*>(GetWindowLongPtrW(window,GWLP_USERDATA));}
LRESULT CALLBACK smo_surface_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=smo_surface(window);
    if(message==WM_NCCREATE){self=static_cast<SmoSurface*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);
    try{
        switch(message){
        case WM_CREATE:self->initialize(window);return 0;
        case WM_SIZE:self->resize();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(window,&paint);EndPaint(window,&paint);self->redraw();return 0;}
        case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owned)delete self;return DefWindowProcW(window,message,wp,lp);
        }
    }catch(const std::exception& failure){self->error=failure.what();if(message==WM_CREATE){OutputDebugStringW(wide(self->error).c_str());return -1;}return 0;}
    return DefWindowProcW(window,message,wp,lp);
}

struct Lab {
    HWND window=nullptr,canvas=nullptr,title=nullptr,status=nullptr,instructions=nullptr,back=nullptr,next=nullptr,load=nullptr,reset=nullptr;
    std::array<HWND,3> backend_buttons{};HFONT heading_font=nullptr,font=nullptr,small_font=nullptr;bool owned=false,benchmark=false,dragging=false;
    unsigned side=8;std::string locale="en";std::optional<WindowsEnvironment> environment;std::function<void()> on_back;
    std::jthread renderer;std::atomic<bool> render_done=true;std::mutex mutex;std::condition_variable wake;
    GraphicsBackend requested_backend=GraphicsBackend::automatic;bool requested_benchmark=false,active=true;unsigned requested_side=8,requested_width=1,requested_height=1;
    std::vector<Command> commands;std::vector<std::shared_ptr<Readback>> queries;GraphicsLabSnapshot snapshot;
    struct Sample{Clock::time_point at;POINT point;};std::deque<Sample> samples;
    explicit Lab(std::optional<WindowsEnvironment> env,std::function<void()> callback,GraphicsBackend initial):environment(std::move(env)),on_back(std::move(callback)),requested_backend(initial){
        if(environment){state::StateStore store(environment->paths.root);locale=interface_language(store.load());}else{wchar_t name[LOCALE_NAME_MAX_LENGTH]{};GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH);locale=utf8(name).substr(0,2);}
    }
    ~Lab(){stop();for(const auto face:{heading_font,font,small_font})if(face)DeleteObject(face);}
    std::string tr(std::string_view es,std::string_view en,std::string_view fr)const{return std::string(locale=="es"?es:locale=="fr"?fr:en);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    void stop(){if(renderer.joinable()){renderer.request_stop();wake.notify_all();
        // DXGI can send synchronous messages to its HWND during Present or
        // destruction. Dispatch only sent messages while joining; never run
        // queued commands which could destroy this owner reentrantly.
        while(!render_done.load()){MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_SENDMESSAGE,MWMO_INPUTAVAILABLE);MSG sent{};PeekMessageW(&sent,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);}renderer.join();}}
    HWND control(const wchar_t* type,std::string_view label,int id,DWORD style=0){const auto text=wide(label);const auto item=CreateWindowExW(0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);if(!item)throw std::runtime_error("Create graphics laboratory control");reader_theme::apply(item,reader_theme::Surface::screen);return item;}
    HWND button(std::string_view label,int id){const auto item=control(L"BUTTON",label,id,WS_TABSTOP|BS_PUSHBUTTON|BS_MULTILINE);reader_theme::primary_button(item);
        // GraphicsDemoActivity uses framework Button surfaces, rather than
        // the accent-filled buttons used by the learning-route screens.
        reader_theme::set_accent(item,RGB(214,215,215));return item;}
    void update_fonts(){
        const auto make=[&](int size,int weight){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");};
        const auto heading=make(21,FW_NORMAL),normal=make(14,FW_NORMAL),detail=make(13,FW_NORMAL);if(!heading||!normal||!detail){for(const auto face:{heading,normal,detail})if(face)DeleteObject(face);throw std::runtime_error("Create graphics laboratory fonts");}
        for(const auto item:{back,next,load,reset,backend_buttons[0],backend_buttons[1],backend_buttons[2]})SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(normal),TRUE);
        SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE);for(const auto item:{status,instructions})SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(detail),TRUE);
        for(const auto face:{heading_font,font,small_font})if(face)DeleteObject(face);heading_font=heading;font=normal;small_font=detail;
    }
    void initialize(HWND target){
        window=target;reader_theme::apply(window,reader_theme::Surface::screen);if(environment){state::StateStore store(environment->paths.root);reader_theme::set_accent(window,reader_theme::appearance_accent(store.load()));}
        back=control(L"BUTTON",tr("Volver","Back","Retour"),back_id,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::icon_button(back);
        title=control(L"STATIC",tr("Laboratorio gráfico · Física / GPU","Graphics laboratory · Physics / GPU","Laboratoire graphique · Physique / GPU"),title_id);reader_theme::set_text_color(title,RGB(255,255,255));
        backend_buttons={button("Auto",auto_id),button("Direct3D 11",hardware_id),button("WARP",warp_id)};
        next=button("",simulation_id);load=button("",load_id);reset=button(tr("Reiniciar objetos","Reset objects","Réinitialiser les objets"),reset_id);
        instructions=control(L"STATIC","",instructions_id);status=control(L"STATIC",tr("Preparando simulación…","Preparing simulation…","Préparation de la simulation…"),status_id);
        reader_theme::set_text_color(instructions,RGB(182,197,215));reader_theme::set_text_color(status,RGB(205,212,226));
        canvas=control(L"STATIC","",canvas_id,SS_BLACKRECT|SS_NOTIFY);SetWindowSubclass(canvas,canvas_proc,1,reinterpret_cast<DWORD_PTR>(this));
        update_fonts();update_controls();layout();render_done.store(false);renderer=std::jthread([this](std::stop_token stop){try{render_loop(stop);}catch(const std::exception& failure){const std::lock_guard lock(mutex);snapshot.error=failure.what();}render_done.store(true);});
    }
    void update_controls(){
        SetWindowTextW(next,wide(benchmark?tr("Simulación anterior: Física interactiva ←","Previous simulation: Interactive physics ←","Simulation précédente : Physique interactive ←"):tr("Siguiente simulación: Benchmark GPU →","Next simulation: GPU benchmark →","Simulation suivante : Benchmark GPU →")).c_str());
        SetWindowTextW(load,wide(tr("Carga GPU: ","GPU load: ","Charge GPU : ")+std::to_string(side*side)+tr(" esferas (cambiar nivel)"," spheres (change level)"," sphères (changer de niveau)")).c_str());
        ShowWindow(load,benchmark?SW_SHOW:SW_HIDE);ShowWindow(reset,benchmark?SW_HIDE:SW_SHOW);
        SetWindowTextW(instructions,wide(benchmark?tr("Geometría 3D animada por GPU mediante instancing. Los FPS incluyen CPU, controladores, presentación y sincronización.","GPU-animated 3D geometry using instancing. FPS includes CPU, drivers, presentation and synchronization.","Géométrie 3D animée par GPU avec instanciation. Les FPS incluent processeur, pilotes, présentation et synchronisation."):
            tr("Haz clic en una esfera, arrástrala y suéltala. La velocidad del mouse determina la fuerza.","Click a sphere, drag it and release it. Mouse speed determines the force.","Cliquez sur une sphère, faites-la glisser et relâchez-la. La vitesse de la souris détermine la force.")).c_str());
        {const std::lock_guard lock(mutex);requested_benchmark=benchmark;requested_side=side;}wake.notify_all();
    }
    void layout(){if(!canvas)return;RECT area{};GetClientRect(window,&area);const int margin=px(12),width=std::max(1,static_cast<int>(area.right)-2*margin),height=static_cast<int>(area.bottom);const bool compact=height<px(480);const int row=px(compact?36:54),title_height=px(compact?32:40);
        MoveWindow(back,margin,margin,px(40),title_height,TRUE);MoveWindow(title,margin+px(48),margin,std::max(1,width-px(48)),title_height,TRUE);int y=margin+title_height+px(8);
        for(int i=0;i<3;++i){const int x=margin+width*i/3;MoveWindow(backend_buttons[static_cast<std::size_t>(i)],x,y,width*(i+1)/3-width*i/3,row,TRUE);}y+=row;
        MoveWindow(next,margin,y,width,row,TRUE);y+=row;MoveWindow(load,margin,y,width,row,TRUE);MoveWindow(reset,margin,y,width,row,TRUE);y+=row;
        const auto dc=GetDC(window);const auto old=SelectObject(dc,small_font);std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(instructions))+1,L'\0');value.resize(GetWindowTextW(instructions,value.data(),static_cast<int>(value.size())));RECT measure{0,0,width,0};DrawTextW(dc,value.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);
        const auto instruction_height=std::max(px(18),static_cast<int>(measure.bottom));MoveWindow(instructions,margin,y+px(5),width,instruction_height,TRUE);y+=instruction_height+px(12);
        const auto status_height=px(compact?48:62);MoveWindow(status,margin,y,width,status_height,TRUE);y+=status_height;
        const auto surface_height=std::max(1,height-y-margin);MoveWindow(canvas,margin,y,width,surface_height,TRUE);
        {const std::lock_guard lock(mutex);requested_width=static_cast<unsigned>(width);requested_height=static_cast<unsigned>(surface_height);active=IsWindowVisible(window)&&!IsIconic(GetAncestor(window,GA_ROOT));}wake.notify_all();
    }
    void enqueue(Command value){{const std::lock_guard lock(mutex);commands.push_back(value);}wake.notify_all();}
    void mouse(UINT message,LPARAM position){
        if(benchmark)return;RECT area{};GetClientRect(canvas,&area);const auto width=std::max(1L,area.right),height=std::max(1L,area.bottom);POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
        const auto now=Clock::now();const auto u=std::clamp(static_cast<float>(point.x)/width,0.0f,1.0f),v=std::clamp(static_cast<float>(point.y)/height,0.0f,1.0f);
        if(message==WM_LBUTTONDOWN){dragging=true;samples.clear();samples.push_back({now,point});SetCapture(canvas);enqueue({Command::Kind::down,u,v});return;}
        if(!dragging)return;samples.push_back({now,point});while(samples.size()>2&&now-samples.front().at>std::chrono::milliseconds(120))samples.pop_front();
        const auto seconds=std::max(0.001,std::chrono::duration<double>(now-samples.front().at).count());const auto vx=static_cast<float>(std::clamp((point.x-samples.front().point.x)/seconds,-8000.0,8000.0)/width),vy=static_cast<float>(std::clamp((point.y-samples.front().point.y)/seconds,-8000.0,8000.0)/height);
        enqueue({message==WM_LBUTTONUP?Command::Kind::up:Command::Kind::move,u,v,vx,vy});if(message==WM_LBUTTONUP){dragging=false;samples.clear();if(GetCapture()==canvas)ReleaseCapture();}
    }
    static LRESULT CALLBACK canvas_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){auto& self=*reinterpret_cast<Lab*>(raw);
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||(message==WM_MOUSEMOVE&&(wp&MK_LBUTTON))){self.mouse(message,lp);return 0;}
        if(message==WM_CANCELMODE||message==WM_CAPTURECHANGED){if(self.dragging){self.dragging=false;self.samples.clear();self.enqueue({Command::Kind::cancel});}return 0;}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(item,canvas_proc,1);return DefSubclassProc(item,message,wp,lp);
    }
    void command(int id){
        if(id==back_id){stop();const auto callback=on_back;if(callback)callback();return;}
        if(id==simulation_id){dragging=false;if(GetCapture()==canvas)ReleaseCapture();enqueue({Command::Kind::cancel});benchmark=!benchmark;update_controls();layout();return;}
        if(id==load_id){side=side==8?16:side==16?32:8;update_controls();return;}
        if(id==reset_id){enqueue({Command::Kind::reset});return;}
        if(id>=auto_id&&id<=warp_id){{const std::lock_guard lock(mutex);requested_backend=id==hardware_id?GraphicsBackend::hardware:id==warp_id?GraphicsBackend::warp:GraphicsBackend::automatic;}wake.notify_all();}
    }
    GraphicsLabSnapshot take(bool pixels){
        if(!pixels){const std::lock_guard lock(mutex);return snapshot;}
        auto query=std::make_shared<Readback>();query->pixels=true;auto reply=query->reply.get_future();{const std::lock_guard lock(mutex);queries.push_back(query);}wake.notify_all();
        const auto deadline=Clock::now()+std::chrono::seconds(10);
        while(reply.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){if(render_done.load()){auto stopped=take(false);if(stopped.error.empty())stopped.error="Graphics renderer is stopped";return stopped;}if(Clock::now()>=deadline)throw std::runtime_error("Graphics readback did not respond");
            // DXGI may send window messages while creating/presenting its real
            // surface. Keep its owner responsive during explicit QA readback.
            MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_SENDMESSAGE,MWMO_INPUTAVAILABLE);MSG sent{};PeekMessageW(&sent,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);}
        return reply.get();
    }
    void publish(const lgp::PhysicsScene& scene,const Gpu& gpu,bool stress,unsigned grid,std::uint64_t frames,double fps){
        GraphicsLabSnapshot value;value.presented_frames=frames;value.benchmark=stress;value.software=gpu.software;value.benchmark_side=grid;value.objects=stress?grid*grid:14;value.width=gpu.width;value.height=gpu.height;value.backend=gpu.description;value.frames_per_second=fps;value.milliseconds_per_frame=fps>0?1000/fps:0;value.selected=scene.selected_index();value.simulation_time=scene.time();value.recent_impact=scene.recent_impact();
        const auto aspect=static_cast<float>(gpu.width)/std::max(1u,gpu.height);for(std::size_t i=0;i<value.bodies.size();++i){const auto& body=scene.get_bodies()[i];value.bodies[i]={body.position.x,body.position.y,body.position.z,body.velocity.x,body.velocity.y,body.velocity.z,body.radius,
            (lgp::PhysicsScene::kFocal/aspect*body.position.x/-body.position.z+1)*0.5f,(1-lgp::PhysicsScene::kFocal*body.position.y/-body.position.z)*0.5f};}
        const std::lock_guard lock(mutex);snapshot=std::move(value);
    }
    void render_loop(std::stop_token stop){
        std::unique_ptr<Gpu> gpu;lgp::PhysicsScene scene;auto preference=GraphicsBackend::automatic;auto started=Clock::now(),stats_at=started;std::uint64_t frames=0,stats_frames=0;double fps=0;float elapsed=0;
        while(!stop.stop_requested()){
            std::vector<Command> inputs;std::vector<std::shared_ptr<Readback>> requests;bool stress=false,visible=false;unsigned grid=8,width=1,height=1;GraphicsBackend selected;
            {std::unique_lock lock(mutex);if(!active&&queries.empty()&&commands.empty())wake.wait(lock,[&]{return stop.stop_requested()||active||!queries.empty()||!commands.empty();});if(stop.stop_requested())break;
                inputs.swap(commands);requests.swap(queries);selected=requested_backend;stress=requested_benchmark;grid=requested_side;width=requested_width;height=requested_height;visible=active;}
            try{
                if(!gpu||selected!=preference){gpu=std::make_unique<Gpu>();gpu->initialize(canvas,selected);preference=selected;scene=lgp::PhysicsScene{};started=Clock::now();stats_at=started;stats_frames=0;fps=0;}
                gpu->resize(width,height);const auto aspect=static_cast<float>(width)/height;scene.set_aspect(aspect);
                for(const auto& input:inputs)switch(input.kind){case Command::Kind::down:scene.touch_down(input.u,input.v,aspect);break;case Command::Kind::move:scene.drag_to(input.u,input.v,aspect);scene.drag_velocity(input.vx,input.vy);break;case Command::Kind::up:scene.drag_to(input.u,input.v,aspect);scene.release(input.vx,input.vy);break;case Command::Kind::cancel:scene.cancel();break;case Command::Kind::reset:scene.reset();break;}
                elapsed=std::chrono::duration<float>(Clock::now()-started).count();if(visible&&!stress)scene.update(elapsed);
                gpu->draw(scene,stress,grid,elapsed,visible);if(visible){++frames;++stats_frames;}
                const auto now=Clock::now();const auto period=std::chrono::duration<double>(now-stats_at).count();const bool statistics=period>=0.5;
                if(statistics){fps=stats_frames/period;stats_frames=0;stats_at=now;}publish(scene,*gpu,stress,grid,frames,fps);
                for(const auto& request:requests){auto value=take(false);if(request->pixels){gpu->draw(scene,stress,grid,elapsed,false);value.colored_pixels=gpu->read_pixels();}request->reply.set_value(std::move(value));}
                if(statistics)PostMessageW(window,stats_message,0,0);
                if(!stress){std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(8),[&]{return stop.stop_requested()||!commands.empty()||!queries.empty()||requested_backend!=preference;});}
            }catch(const std::exception& error){GraphicsLabSnapshot value;{const std::lock_guard lock(mutex);snapshot.error=error.what();value=snapshot;}for(const auto& request:requests)request->reply.set_value(value);PostMessageW(window,stats_message,0,0);
                gpu.reset();std::unique_lock lock(mutex);wake.wait(lock,[&]{return stop.stop_requested()||requested_backend!=selected||!queries.empty();});}
        }
        std::vector<std::shared_ptr<Readback>> abandoned;{const std::lock_guard lock(mutex);abandoned.swap(queries);}for(const auto& query:abandoned)query->reply.set_value(snapshot);
    }
    void show_stats(){const auto value=take(false);if(!value.error.empty()){SetWindowTextW(status,wide(tr("Motor gráfico: ","Graphics engine: ","Moteur graphique : ")+value.error).c_str());return;}
        std::ostringstream output;output<<value.backend<<'\n';if(value.benchmark)output<<tr("Benchmark GPU · ","GPU benchmark · ","Benchmark GPU · ")<<value.objects<<tr(" esferas · "," spheres · "," sphères · ")<<value.objects*sphere_vertices<<tr(" vértices/fotograma"," vertices/frame"," sommets/image");
        else{output<<tr("Física interactiva · 14 esferas | gravedad 9.81 m/s² | impacto ","Interactive physics · 14 spheres | gravity 9.81 m/s² | impact ","Physique interactive · 14 sphères | gravité 9.81 m/s² | impact ")<<std::fixed<<std::setprecision(1)<<value.recent_impact<<" m/s";if(value.selected>=0)output<<tr(" | objeto "," | object "," | objet ")<<value.selected+1<<tr(" seleccionado"," selected"," sélectionné");}
        output<<'\n'<<std::fixed<<std::setprecision(1)<<value.frames_per_second<<" FPS · "<<std::setprecision(2)<<value.milliseconds_per_frame<<tr(" ms/fotograma (tiempo total)"," ms/frame (total time)"," ms/image (temps total)");SetWindowTextW(status,wide(output.str()).c_str());
    }
};
Lab* lab(HWND window){return reinterpret_cast<Lab*>(GetWindowLongPtrW(window,GWLP_USERDATA));}
LRESULT CALLBACK lab_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=lab(window);if(message==WM_NCCREATE){self=static_cast<Lab*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,wp,lp);
    try{LRESULT color=0;if(reader_theme::control_color(message,wp,lp,color))return color;switch(message){case WM_CREATE:self->initialize(window);return 0;case WM_SIZE:self->layout();return 0;case WM_DPICHANGED_AFTERPARENT:self->update_fonts();self->layout();return 0;
        case WM_SHOWWINDOW:{const std::lock_guard lock(self->mutex);self->active=wp!=0;self->wake.notify_all();break;}
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)self->command(LOWORD(wp));return 0;case stats_message:self->show_stats();return 0;case WM_KEYDOWN:if(wp==VK_ESCAPE){self->command(back_id);return 0;}break;
        case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item.CtlID==back_id){(void)reader_theme::draw_button(item);auto bounds=item.rcItem;InflateRect(&bounds,-self->px(10),-self->px(10));draw_icon(item.hDC,bounds,Icon::back,reader_theme::palette(window).text);return TRUE;}if(reader_theme::draw_button(item))return TRUE;break;}
        case WM_ERASEBKGND:{RECT rect{};GetClientRect(window,&rect);FillRect(reinterpret_cast<HDC>(wp),&rect,reader_theme::brush(reader_theme::Surface::screen));return 1;}
        case WM_DESTROY:self->stop();return 0;case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owned)delete self;return DefWindowProcW(window,message,wp,lp);}
    }catch(const std::exception& error){if(message==WM_CREATE){OutputDebugStringW(wide(error.what()).c_str());return -1;}if(self->status)SetWindowTextW(self->status,wide(error.what()).c_str());}return DefWindowProcW(window,message,wp,lp);
}
}
HWND create_physics_graphics_view(HWND parent,std::optional<WindowsEnvironment> environment,std::function<void()> on_back,GraphicsBackend initial){
    static const bool registered=[](){WNDCLASSW type{};type.lpfnWndProc=lab_proc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=lab_class;return RegisterClassW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;}();if(!registered)throw std::runtime_error("Register graphics laboratory");
    auto data=std::make_unique<Lab>(std::move(environment),std::move(on_back),initial);const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,lab_class,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),data.get());if(!window)throw std::runtime_error("Create graphics laboratory");data->owned=true;data.release();return window;
}
GraphicsLabSnapshot inspect_graphics_lab(HWND window,bool pixels){wchar_t name[100]{};GetClassNameW(window,name,100);if(std::wstring_view(name)!=lab_class||!lab(window))throw std::invalid_argument("Not a live graphics laboratory");return lab(window)->take(pixels);}
GraphicsFrame inspect_physics_graphics_frame(HWND window){const auto frame=inspect_graphics_lab(window);return {frame.presented_frames,frame.colored_pixels,frame.simulation_time,false,frame.backend};}
HWND create_smo_surface(HWND parent,GraphicsBackend initial){
    static const bool registered=[](){WNDCLASSW type{};type.lpfnWndProc=smo_surface_proc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=smo_surface_class;
        return RegisterClassW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;}();
    if(!registered)throw std::runtime_error("Register SMO graphics surface");
    auto data=std::make_unique<SmoSurface>(initial);
    const auto window=CreateWindowExW(0,smo_surface_class,L"SMO",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),data.get());
    if(!window)throw std::runtime_error("Create SMO graphics surface");data->owned=true;data.release();return window;
}
void draw_smo_surface(HWND window,const smo::Frame& frame){
    wchar_t name[100]{};GetClassNameW(window,name,100);auto* self=smo_surface(window);
    if(std::wstring_view(name)!=smo_surface_class||!self)throw std::invalid_argument("Not a live SMO graphics surface");
    try{self->submit(frame);}catch(const std::exception& failure){self->error=failure.what();throw;}
}
SmoSurfaceSnapshot inspect_smo_surface(HWND window,bool pixels){
    wchar_t name[100]{};GetClassNameW(window,name,100);auto* self=smo_surface(window);
    if(std::wstring_view(name)!=smo_surface_class||!self)throw std::invalid_argument("Not a live SMO graphics surface");
    try{return self->inspect(pixels);}catch(const std::exception& failure){self->error=failure.what();return self->inspect(false);}
}
}
