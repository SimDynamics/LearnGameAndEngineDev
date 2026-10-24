#include "presentation/windows/smo_compute.hpp"

#include <windows.h>
#include <gl/GL.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace lgaed::presentation {
namespace {
// Windows ships the GL1.1 header. These narrow API declarations/constants are
// taken from the Khronos registry, not from a bundled SDK or alternate driver:
// https://github.com/KhronosGroup/OpenGL-Registry/blob/main/api/GL/glcorearb.h
// https://github.com/KhronosGroup/OpenGL-Registry/blob/main/api/GL/wglext.h
using GlSize=std::ptrdiff_t;
using GlInt64=std::int64_t;
using GlUInt64=std::uint64_t;
struct GlSyncOpaque;
using GlSync=GlSyncOpaque*;
constexpr GLenum storage_buffer=0x90D2,uniform_buffer=0x8A11,static_draw=0x88E4,dynamic_draw=0x88E8,
    compute_shader=0x91B9,compile_status=0x8B81,link_status=0x8B82,info_log_length=0x8B84,
    major_version=0x821B,minor_version=0x821C,shading_language_version=0x8B8C,
    max_storage_bindings=0x90DD,max_compute_storage_blocks=0x90DB,max_storage_block_size=0x90DE,
    max_uniform_bindings=0x8A2F,max_group_count=0x91BE,r32ui=0x8236,red_integer=0x8D94,
    timestamp=0x8E28,query_counter_bits=0x8864,query_available=0x8867,query_result=0x8866,
    sync_complete=0x9117,already_signaled=0x911A,timeout_expired=0x911B,condition_satisfied=0x911C,wait_failed=0x911D,
    active_subroutines=0x8DE5,active_subroutine_locations=0x8E47;
constexpr GLbitfield storage_barrier=0x2000,buffer_update_barrier=0x200;
constexpr int context_major=0x2091,context_minor=0x2092,context_profile=0x9126,core_profile=1;
using Clock=std::chrono::steady_clock;
constexpr auto wait_budget=std::chrono::seconds(30);
constexpr double no_clock=std::numeric_limits<double>::quiet_NaN();

void require(bool value,std::string_view what){if(!value)throw std::runtime_error(std::string(what));}
void cancelled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Prueba GPU OpenGL cancelada.");}
std::string lower(std::string value){std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}

struct Api {
    HMODULE library=nullptr;
    HGLRC (WINAPI* CreateContext)(HDC)=nullptr;
    BOOL (WINAPI* DeleteContext)(HGLRC)=nullptr;
    BOOL (WINAPI* MakeCurrent)(HDC,HGLRC)=nullptr;
    HGLRC (WINAPI* GetCurrentContext)()=nullptr;
    HDC (WINAPI* GetCurrentDC)()=nullptr;
    PROC (WINAPI* GetProc)(LPCSTR)=nullptr;
    HGLRC (WINAPI* CreateContextAttribs)(HDC,HGLRC,const int*)=nullptr;

#define SMO_GL_PROC(result,name,...) using name##Proc=result(APIENTRY*)(__VA_ARGS__);name##Proc name=nullptr
    SMO_GL_PROC(GLenum,GetError);
    SMO_GL_PROC(const GLubyte*,GetString,GLenum);
    SMO_GL_PROC(void,GetIntegerv,GLenum,GLint*);
    SMO_GL_PROC(void,GetInteger64v,GLenum,GlInt64*);
    SMO_GL_PROC(void,GetIntegeri_v,GLenum,GLuint,GLint*);
    SMO_GL_PROC(void,Flush);
    SMO_GL_PROC(void,GenBuffers,GLsizei,GLuint*);
    SMO_GL_PROC(void,DeleteBuffers,GLsizei,const GLuint*);
    SMO_GL_PROC(void,BindBuffer,GLenum,GLuint);
    SMO_GL_PROC(void,BufferData,GLenum,GlSize,const void*,GLenum);
    SMO_GL_PROC(void,BufferSubData,GLenum,GlSize,GlSize,const void*);
    SMO_GL_PROC(void,GetBufferSubData,GLenum,GlSize,GlSize,void*);
    SMO_GL_PROC(void,ClearBufferData,GLenum,GLenum,GLenum,GLenum,const void*);
    SMO_GL_PROC(void,BindBufferBase,GLenum,GLuint,GLuint);
    SMO_GL_PROC(GLuint,CreateShader,GLenum);
    SMO_GL_PROC(void,ShaderSource,GLuint,GLsizei,const char* const*,const GLint*);
    SMO_GL_PROC(void,CompileShader,GLuint);
    SMO_GL_PROC(void,GetShaderiv,GLuint,GLenum,GLint*);
    SMO_GL_PROC(void,GetShaderInfoLog,GLuint,GLsizei,GLsizei*,char*);
    SMO_GL_PROC(void,DeleteShader,GLuint);
    SMO_GL_PROC(GLuint,CreateProgram);
    SMO_GL_PROC(void,AttachShader,GLuint,GLuint);
    SMO_GL_PROC(void,LinkProgram,GLuint);
    SMO_GL_PROC(void,GetProgramiv,GLuint,GLenum,GLint*);
    SMO_GL_PROC(void,GetProgramInfoLog,GLuint,GLsizei,GLsizei*,char*);
    SMO_GL_PROC(void,DeleteProgram,GLuint);
    SMO_GL_PROC(void,UseProgram,GLuint);
    SMO_GL_PROC(void,GetProgramStageiv,GLuint,GLenum,GLenum,GLint*);
    SMO_GL_PROC(GLint,GetSubroutineUniformLocation,GLuint,GLenum,const char*);
    SMO_GL_PROC(GLuint,GetSubroutineIndex,GLuint,GLenum,const char*);
    SMO_GL_PROC(void,UniformSubroutinesuiv,GLenum,GLsizei,const GLuint*);
    SMO_GL_PROC(void,DispatchCompute,GLuint,GLuint,GLuint);
    SMO_GL_PROC(void,MemoryBarrier,GLbitfield);
    SMO_GL_PROC(void,GenQueries,GLsizei,GLuint*);
    SMO_GL_PROC(void,DeleteQueries,GLsizei,const GLuint*);
    SMO_GL_PROC(void,QueryCounter,GLuint,GLenum);
    SMO_GL_PROC(void,GetQueryiv,GLenum,GLenum,GLint*);
    SMO_GL_PROC(void,GetQueryObjectiv,GLuint,GLenum,GLint*);
    SMO_GL_PROC(void,GetQueryObjectui64v,GLuint,GLenum,GlUInt64*);
    SMO_GL_PROC(GlSync,FenceSync,GLenum,GLbitfield);
    SMO_GL_PROC(GLenum,ClientWaitSync,GlSync,GLbitfield,GlUInt64);
    SMO_GL_PROC(void,DeleteSync,GlSync);
#undef SMO_GL_PROC

    template<class Function>Function exported(const char* name){const auto proc=GetProcAddress(library,name);
        require(proc!=nullptr,std::string("OpenGL de Windows no exporta ")+name);return std::bit_cast<Function>(proc);}
    template<class Function>Function extension(const char* name,bool required=true){
        auto proc=GetProc(name);const auto address=std::bit_cast<std::intptr_t>(proc);
        // Some ICDs return these failure sentinels instead of null.
        if(!proc||address==1||address==2||address==3||address==-1)proc=std::bit_cast<PROC>(GetProcAddress(library,name));
        if(required)require(proc!=nullptr,std::string("El contexto OpenGL no ofrece ")+name);
        return std::bit_cast<Function>(proc);
    }
    void system(){library=LoadLibraryExW(L"opengl32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(library!=nullptr,"No se pudo cargar el OpenGL oficial de Windows.");
        CreateContext=exported<decltype(CreateContext)>("wglCreateContext");DeleteContext=exported<decltype(DeleteContext)>("wglDeleteContext");
        MakeCurrent=exported<decltype(MakeCurrent)>("wglMakeCurrent");GetCurrentContext=exported<decltype(GetCurrentContext)>("wglGetCurrentContext");
        GetCurrentDC=exported<decltype(GetCurrentDC)>("wglGetCurrentDC");GetProc=exported<decltype(GetProc)>("wglGetProcAddress");
    }
    void context(){
#define SMO_GL_LOAD(name) name=extension<name##Proc>("gl" #name)
        SMO_GL_LOAD(GetError);SMO_GL_LOAD(GetString);SMO_GL_LOAD(GetIntegerv);SMO_GL_LOAD(GetInteger64v);SMO_GL_LOAD(GetIntegeri_v);SMO_GL_LOAD(Flush);
        SMO_GL_LOAD(GenBuffers);SMO_GL_LOAD(DeleteBuffers);SMO_GL_LOAD(BindBuffer);SMO_GL_LOAD(BufferData);SMO_GL_LOAD(BufferSubData);SMO_GL_LOAD(GetBufferSubData);
        SMO_GL_LOAD(ClearBufferData);SMO_GL_LOAD(BindBufferBase);SMO_GL_LOAD(CreateShader);SMO_GL_LOAD(ShaderSource);SMO_GL_LOAD(CompileShader);
        SMO_GL_LOAD(GetShaderiv);SMO_GL_LOAD(GetShaderInfoLog);SMO_GL_LOAD(DeleteShader);SMO_GL_LOAD(CreateProgram);SMO_GL_LOAD(AttachShader);
        SMO_GL_LOAD(LinkProgram);SMO_GL_LOAD(GetProgramiv);SMO_GL_LOAD(GetProgramInfoLog);SMO_GL_LOAD(DeleteProgram);SMO_GL_LOAD(UseProgram);
        SMO_GL_LOAD(DispatchCompute);SMO_GL_LOAD(MemoryBarrier);SMO_GL_LOAD(FenceSync);SMO_GL_LOAD(ClientWaitSync);SMO_GL_LOAD(DeleteSync);
#undef SMO_GL_LOAD
#define SMO_GL_OPTIONAL(name) name=extension<name##Proc>("gl" #name,false)
        SMO_GL_OPTIONAL(GenQueries);SMO_GL_OPTIONAL(DeleteQueries);SMO_GL_OPTIONAL(QueryCounter);SMO_GL_OPTIONAL(GetQueryiv);
        SMO_GL_OPTIONAL(GetQueryObjectiv);SMO_GL_OPTIONAL(GetQueryObjectui64v);
        SMO_GL_OPTIONAL(GetProgramStageiv);SMO_GL_OPTIONAL(GetSubroutineUniformLocation);SMO_GL_OPTIONAL(GetSubroutineIndex);SMO_GL_OPTIONAL(UniformSubroutinesuiv);
#undef SMO_GL_OPTIONAL
    }
    bool errors(){bool any=false;for(unsigned n=0;n<32&&GetError()!=GL_NO_ERROR;++n)any=true;return any;}
    void check(std::string_view operation){const auto error=GetError();if(error!=GL_NO_ERROR){(void)errors();
        throw std::runtime_error(std::string(operation)+": error OpenGL "+std::to_string(error)+".");}}
    std::string text(GLenum name){const auto result=GetString(name);return result?reinterpret_cast<const char*>(result):"";}
};

class OpenGlCompute final:public SmoCompute {
    struct Buffer {GLuint name=0;std::size_t bytes=0;};
    struct Program {GLuint name=0;std::vector<GLuint> subroutines;};
    Api gl_;HWND window_=nullptr;HDC dc_=nullptr;HGLRC context_=nullptr;std::wstring class_name_;
    bool registered_=false,poisoned_=false;DWORD owner_=GetCurrentThreadId();std::stop_token stop_;
    SmoComputeInfo info_;std::vector<Buffer> buffers_;std::vector<Program> programs_{{0,{}}};
    GLuint parameters_=0;std::array<GLuint,2> queries_{};std::array<GLuint,3> max_groups_{};
    std::size_t max_buffer_=0;GLint clock_bits_=0;

    struct Current {
        OpenGlCompute& self;HGLRC previous=nullptr;HDC dc=nullptr;bool switched=false;
        explicit Current(OpenGlCompute& item):self(item){
            require(GetCurrentThreadId()==self.owner_,"El ejecutor OpenGL debe utilizarse en el hilo que lo creó.");
            previous=self.gl_.GetCurrentContext();dc=self.gl_.GetCurrentDC();switched=previous!=self.context_;
            if(switched)require(self.gl_.MakeCurrent(self.dc_,self.context_)!=FALSE,"No se pudo activar el contexto GPU OpenGL.");
        }
        ~Current(){if(switched)self.gl_.MakeCurrent(dc,previous);}
    };
    void ready(){cancelled(stop_);require(!poisoned_,"El ejecutor OpenGL quedó incompleto; debe recrearse antes de reutilizarlo.");}
    void release(HGLRC previous=nullptr,HDC previous_dc=nullptr)noexcept{
        if(context_&&gl_.MakeCurrent){
            if(previous==context_){previous=nullptr;previous_dc=nullptr;}
            if(gl_.MakeCurrent(dc_,context_)){
                if(gl_.UseProgram)gl_.UseProgram(0);
                if(gl_.DeleteProgram)for(const auto& program:programs_)if(program.name)gl_.DeleteProgram(program.name);
                if(gl_.DeleteBuffers){for(const auto& resource:buffers_)if(resource.name)gl_.DeleteBuffers(1,&resource.name);
                    if(parameters_)gl_.DeleteBuffers(1,&parameters_);}
                if(gl_.DeleteQueries&&(queries_[0]||queries_[1]))gl_.DeleteQueries(2,queries_.data());
                gl_.MakeCurrent(nullptr,nullptr);
            }
            if(gl_.DeleteContext)gl_.DeleteContext(context_);context_=nullptr;
            gl_.MakeCurrent(previous_dc,previous);
        }
        if(dc_&&window_)ReleaseDC(window_,dc_);dc_=nullptr;
        if(window_)DestroyWindow(window_);window_=nullptr;
        if(registered_)UnregisterClassW(class_name_.c_str(),GetModuleHandleW(nullptr));registered_=false;
        if(gl_.library)FreeLibrary(gl_.library);gl_.library=nullptr;
    }
    void create_context(){
        class_name_=L"LearnGameAndEngineDev.SmoCompute.OpenGL."+std::to_wstring(GetCurrentProcessId())+L"."+
            std::to_wstring(reinterpret_cast<std::uintptr_t>(this));
        WNDCLASSW type{};type.style=CS_OWNDC;type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=class_name_.c_str();
        require(RegisterClassW(&type)!=0,"No se pudo registrar la ventana privada de cálculo OpenGL.");registered_=true;
        // Never show or focus this owned compute-only drawable.
        window_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,class_name_.c_str(),L"",WS_POPUP,0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
        require(window_!=nullptr,"No se pudo crear la superficie privada de cálculo OpenGL.");dc_=GetDC(window_);
        require(dc_!=nullptr,"No se pudo obtener el contexto de dispositivo OpenGL.");
        PIXELFORMATDESCRIPTOR format{};format.nSize=sizeof(format);format.nVersion=1;format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;
        format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;format.cAlphaBits=8;format.iLayerType=PFD_MAIN_PLANE;
        const auto chosen=ChoosePixelFormat(dc_,&format);require(chosen!=0&&SetPixelFormat(dc_,chosen,&format)!=FALSE,"El controlador no ofrece un formato OpenGL para Windows.");
        context_=gl_.CreateContext(dc_);require(context_!=nullptr&&gl_.MakeCurrent(dc_,context_)!=FALSE,"No se pudo inicializar WGL.");
        gl_.CreateContextAttribs=gl_.extension<decltype(gl_.CreateContextAttribs)>("wglCreateContextAttribsARB");
        const int attributes[]{context_major,4,context_minor,3,context_profile,core_profile,0};
        const auto modern=gl_.CreateContextAttribs(dc_,nullptr,attributes);require(modern!=nullptr,"El controlador instalado no ofrece OpenGL4.3 core con compute shaders.");
        gl_.MakeCurrent(nullptr,nullptr);gl_.DeleteContext(context_);context_=modern;
        require(gl_.MakeCurrent(dc_,context_)!=FALSE,"No se pudo activar OpenGL4.3 core.");
        // Procedure addresses are fetched again for the actual modern context.
        gl_.context();GLint major=0,minor=0;gl_.GetIntegerv(major_version,&major);gl_.GetIntegerv(minor_version,&minor);
        require(major>4||(major==4&&minor>=3),"La versión real del contexto es inferior a OpenGL4.3.");
        info_.api="OpenGL "+std::to_string(major)+"."+std::to_string(minor)+" compute";
    }
    void configure(){
        const auto vendor=gl_.text(GL_VENDOR);info_.adapter=gl_.text(GL_RENDERER);
        info_.driver=vendor+" / "+gl_.text(GL_VERSION)+" / GLSL "+gl_.text(shading_language_version);
        require(!info_.adapter.empty(),"El controlador no identificó el adaptador real de OpenGL.");
        const auto manufacturer=lower(vendor),adapter=lower(info_.adapter);
        info_.software=adapter.find("software")!=adapter.npos||adapter.find("llvmpipe")!=adapter.npos||adapter.find("softpipe")!=adapter.npos||
            adapter.find("swiftshader")!=adapter.npos||adapter.find("gdi generic")!=adapter.npos;
        if(manufacturer.find("nvidia")!=manufacturer.npos)info_.vendor_id=0x10de;
        else if(manufacturer.find("amd")!=manufacturer.npos||manufacturer.starts_with("ati")||manufacturer.find("advanced micro devices")!=manufacturer.npos)info_.vendor_id=0x1002;
        else if(manufacturer.find("intel")!=manufacturer.npos)info_.vendor_id=0x8086;
        // No portable WGL API selects a DXGI adapter or returns its PCI device ID.
        // Report the ICD's actual renderer; the caller compares adapter identity.
        GLint storage=0,compute=0,uniform=0;GlInt64 block_size=0;
        gl_.GetIntegerv(max_storage_bindings,&storage);gl_.GetIntegerv(max_compute_storage_blocks,&compute);
        gl_.GetIntegerv(max_uniform_bindings,&uniform);gl_.GetInteger64v(max_storage_block_size,&block_size);
        require(storage>=8&&compute>=8&&uniform>8&&block_size>=64,"OpenGL no dispone de los ocho SSBO y del UBO8 requeridos.");
        max_buffer_=static_cast<std::size_t>(std::min<GlInt64>(block_size,std::numeric_limits<GlSize>::max()));
        for(GLuint axis=0;axis<3;++axis){GLint maximum=0;gl_.GetIntegeri_v(max_group_count,axis,&maximum);
            require(maximum>0,"El controlador OpenGL informó un límite de despacho inválido.");max_groups_[axis]=static_cast<GLuint>(maximum);}
        gl_.check("Consultar capacidades reales de OpenGL");
        if(gl_.GenQueries&&gl_.DeleteQueries&&gl_.QueryCounter&&gl_.GetQueryiv&&gl_.GetQueryObjectiv&&gl_.GetQueryObjectui64v){
            gl_.GetQueryiv(timestamp,query_counter_bits,&clock_bits_);
            if(gl_.errors())clock_bits_=0;
            info_.timestamps=clock_bits_>=30&&clock_bits_<=64;
            if(info_.timestamps){gl_.GenQueries(2,queries_.data());if(gl_.errors()||!queries_[0]||!queries_[1])info_.timestamps=false;}
        }
        const std::array<std::uint32_t,16> zeros{};
        buffers_.push_back(make_buffer(sizeof(zeros),std::as_bytes(std::span(zeros))));
        gl_.GenBuffers(1,&parameters_);require(parameters_!=0,"No se pudo crear el bloque de parámetros OpenGL.");
        gl_.BindBuffer(uniform_buffer,parameters_);gl_.BufferData(uniform_buffer,64,zeros.data(),dynamic_draw);
        gl_.BindBufferBase(uniform_buffer,8,parameters_);gl_.check("Crear parámetros comunes OpenGL");
    }
    Buffer make_buffer(std::size_t bytes,std::span<const std::byte> initial){
        require(bytes>0&&bytes%sizeof(std::uint32_t)==0&&bytes<=max_buffer_,"El tamaño del SSBO OpenGL debe ser un múltiplo de cuatro dentro del límite real del controlador.");
        require(initial.size()<=bytes,"Los datos iniciales exceden el SSBO OpenGL.");Buffer result{0,bytes};gl_.GenBuffers(1,&result.name);
        require(result.name!=0,"No se pudo crear el SSBO OpenGL.");
        try{gl_.BindBuffer(storage_buffer,result.name);
            if(initial.size()==bytes)gl_.BufferData(storage_buffer,static_cast<GlSize>(bytes),initial.data(),static_draw);
            else{gl_.BufferData(storage_buffer,static_cast<GlSize>(bytes),nullptr,static_draw);const GLuint zero=0;
                gl_.ClearBufferData(storage_buffer,r32ui,red_integer,GL_UNSIGNED_INT,&zero);
                if(!initial.empty())gl_.BufferSubData(storage_buffer,0,static_cast<GlSize>(initial.size()),initial.data());}
            gl_.check("Asignar y cargar SSBO OpenGL");return result;
        }catch(...){gl_.DeleteBuffers(1,&result.name);throw;}
    }
    const Buffer& resource(SmoBuffer id,bool dummy=false)const{
        require((dummy||id!=0)&&id<buffers_.size(),"Identificador de SSBO OpenGL inválido.");return buffers_[id];
    }
    std::string shader_log(GLuint object,bool program){GLint length=0;
        if(program)gl_.GetProgramiv(object,info_log_length,&length);else gl_.GetShaderiv(object,info_log_length,&length);
        if(length<1)return "sin diagnóstico del controlador";length=std::min(length,64*1024);std::string result(static_cast<std::size_t>(length),'\0');GLsizei written=0;
        if(program)gl_.GetProgramInfoLog(object,length,&written,result.data());else gl_.GetShaderInfoLog(object,length,&written,result.data());
        if(written>=0&&written<length)result.resize(static_cast<std::size_t>(written));return result;
    }
    std::vector<GLuint> subroutine_bindings(GLuint program,std::span<const SmoGlSubroutineBinding> bindings){
        if(bindings.empty())return {};
        require(gl_.GetProgramStageiv&&gl_.GetSubroutineUniformLocation&&gl_.GetSubroutineIndex&&gl_.UniformSubroutinesuiv,
            "El contexto OpenGL no ofrece las subrutinas GLSL requeridas por este kernel.");
        GLint locations=0,functions=0;gl_.GetProgramStageiv(program,compute_shader,active_subroutine_locations,&locations);
        gl_.GetProgramStageiv(program,compute_shader,active_subroutines,&functions);gl_.check("Consultar subrutinas activas del kernel OpenGL");
        require(locations>=0&&functions>=0,"OpenGL informó una cantidad de subrutinas inválida.");
        constexpr auto invalid=std::numeric_limits<GLuint>::max();std::vector<GLuint> result(static_cast<std::size_t>(locations),invalid);
        for(const auto& binding:bindings){const auto location=gl_.GetSubroutineUniformLocation(program,compute_shader,binding.uniform.c_str());
            gl_.check("Consultar uniforme de subrutina OpenGL "+binding.uniform);if(location==-1)continue;
            require(location>=0&&location<locations,"La ubicación de una subrutina OpenGL está fuera del rango activo.");
            const auto index=gl_.GetSubroutineIndex(program,compute_shader,binding.function.c_str());gl_.check("Consultar función de subrutina OpenGL "+binding.function);
            require(index!=invalid&&index<static_cast<GLuint>(functions),"La implementación de subrutina OpenGL no es una función activa válida: "+binding.function);
            auto& selected=result[static_cast<std::size_t>(location)];require(selected==invalid||selected==index,"Una ubicación de subrutina OpenGL tiene enlaces contradictorios.");selected=index;}
        require(std::ranges::none_of(result,[&](auto index){return index==invalid;}),"Falta la implementación explícita de una subrutina activa OpenGL.");
        return result;
    }
    void wait(GlSync fence,Clock::time_point deadline,bool& interrupted){
        const auto early_poll_until=Clock::now()+std::chrono::microseconds(250);
        for(;;){interrupted=interrupted||stop_.stop_requested();const auto result=gl_.ClientWaitSync(fence,0,0);
            if(result==already_signaled||result==condition_satisfied)return;
            require(result!=wait_failed,"Falló la espera de la cola GPU OpenGL.");require(result==timeout_expired,"OpenGL devolvió un estado de sincronización desconocido.");
            const auto now=Clock::now();require(now<deadline,"La cola GPU OpenGL no terminó dentro de la espera acotada.");
            // Fast GPU submissions often complete in microseconds. Neither a
            // millisecond sleep nor a driver timeout rounded to a Windows
            // scheduler tick should leave that GPU idle before the next pass.
            // After a short poll, yield runnable CPU work without a timer or
            // changing the PC's global timer resolution. The fence is still
            // queried for real completion; this never substitutes CPU time.
            if(now<early_poll_until||!SwitchToThread())YieldProcessor();
        }
    }
public:
    OpenGlCompute(const SmoComputeOptions& options,std::stop_token stop):stop_(stop){
        cancelled(stop_);HGLRC previous=nullptr;HDC previous_dc=nullptr;
        try{gl_.system();previous=gl_.GetCurrentContext();previous_dc=gl_.GetCurrentDC();create_context();configure();
            require(!options.software||info_.software,"OpenGL del controlador actual no ofrece el adaptador software solicitado; WARP sólo pertenece a Direct3D.");
            cancelled(stop_);require(gl_.MakeCurrent(previous_dc,previous)!=FALSE,"No se pudo restaurar el contexto OpenGL anterior.");
        }catch(...){release(previous,previous_dc);throw;}
    }
    ~OpenGlCompute()override{const auto previous=gl_.GetCurrentContext?gl_.GetCurrentContext():nullptr;
        const auto dc=gl_.GetCurrentDC?gl_.GetCurrentDC():nullptr;release(previous,dc);}
    SmoComputeInfo info()const override{return info_;}
    SmoBuffer buffer(std::size_t bytes,std::span<const std::byte> initial)override{
        ready();Current current(*this);require(buffers_.size()<std::numeric_limits<SmoBuffer>::max(),"Demasiados recursos OpenGL.");
        auto result=make_buffer(bytes,initial);try{buffers_.push_back(result);}catch(...){gl_.DeleteBuffers(1,&result.name);throw;}
        return static_cast<SmoBuffer>(buffers_.size()-1);
    }
    SmoKernel kernel(const SmoKernelSource& source)override{
        ready();Current current(*this);require(!source.glsl.empty()&&source.glsl.size()<=static_cast<std::size_t>(std::numeric_limits<GLint>::max()),"Fuente de compute shader GLSL vacía o excesiva.");
        require(programs_.size()<std::numeric_limits<SmoKernel>::max(),"Demasiados programas OpenGL.");
        GLuint shader=gl_.CreateShader(compute_shader),program=0;require(shader!=0,"No se pudo crear el compute shader OpenGL.");
        try{const auto* text=source.glsl.data();const auto length=static_cast<GLint>(source.glsl.size());gl_.ShaderSource(shader,1,&text,&length);
            // A non-debug context uses the installed GPU compiler's normal
            // optimization. OpenGL exposes no portable /O3 switch. The exact
            // source, including its precise qualifiers, is not rewritten.
            gl_.CompileShader(shader);GLint compiled=0;gl_.GetShaderiv(shader,compile_status,&compiled);
            require(compiled!=GL_FALSE,"Compilar GLSL "+source.name+": "+shader_log(shader,false));cancelled(stop_);
            program=gl_.CreateProgram();require(program!=0,"No se pudo crear el programa GPU OpenGL.");gl_.AttachShader(program,shader);gl_.LinkProgram(program);
            GLint linked=0;gl_.GetProgramiv(program,link_status,&linked);require(linked!=GL_FALSE,"Enlazar GLSL "+source.name+": "+shader_log(program,true));
            gl_.check("Compilar y enlazar compute shader OpenGL");auto subroutines=subroutine_bindings(program,source.gl_subroutines);
            cancelled(stop_);programs_.push_back({program,std::move(subroutines)});gl_.DeleteShader(shader);
            return static_cast<SmoKernel>(programs_.size()-1);
        }catch(...){if(program)gl_.DeleteProgram(program);gl_.DeleteShader(shader);throw;}
    }
    void write(SmoBuffer id,std::span<const std::byte> bytes)override{
        ready();Current current(*this);const auto& target=resource(id);require(bytes.size()<=target.bytes,"La escritura excede el SSBO OpenGL.");
        if(bytes.empty())return;gl_.MemoryBarrier(buffer_update_barrier);gl_.BindBuffer(storage_buffer,target.name);
        gl_.BufferSubData(storage_buffer,0,static_cast<GlSize>(bytes.size()),bytes.data());gl_.check("Actualizar SSBO OpenGL");cancelled(stop_);
    }
    std::vector<std::byte> read(SmoBuffer id)override{
        ready();Current current(*this);const auto& source=resource(id);std::vector<std::byte> result(source.bytes);
        gl_.MemoryBarrier(buffer_update_barrier);gl_.BindBuffer(storage_buffer,source.name);
        gl_.GetBufferSubData(storage_buffer,0,static_cast<GlSize>(source.bytes),result.data());gl_.check("Leer SSBO GPU OpenGL");cancelled(stop_);return result;
    }
    double execute(std::span<const SmoDispatch> dispatches,bool timed)override{
        ready();Current current(*this);
        // Validate the complete sequence before submitting any GPU work.
        for(const auto& dispatch:dispatches){require(dispatch.kernel!=0&&dispatch.kernel<programs_.size(),"Identificador de compute shader OpenGL inválido.");
            for(const auto id:dispatch.buffers)(void)resource(id,true);
            for(std::size_t axis=0;axis<3;++axis)require(dispatch.groups[axis]<=max_groups_[axis],"El despacho excede el límite real de grupos OpenGL.");}
        const bool measured=timed&&info_.timestamps;const auto began=Clock::now();GlSync fence=nullptr;bool interrupted=false;
        try{
            if(measured)gl_.QueryCounter(queries_[0],timestamp);
            for(const auto& dispatch:dispatches){
                // Stop submitting at the same dispatch boundary as before,
                // but retain/drain work already queued before reporting a
                // normal cancellation. A hung GPU still hits the same bounded
                // failure; cancellation is not a driver-reset guarantee.
                if(stop_.stop_requested()){interrupted=true;break;}
                const auto& program=programs_[dispatch.kernel];gl_.UseProgram(program.name);
                if(!program.subroutines.empty()){
                    // Subroutine uniforms are context state reset by UseProgram,
                    // so select every active location again before EACH dispatch.
                    // https://registry.khronos.org/OpenGL/extensions/ARB/ARB_shader_subroutine.txt
                    gl_.UniformSubroutinesuiv(compute_shader,static_cast<GLsizei>(program.subroutines.size()),program.subroutines.data());
                    gl_.check("Aplicar implementaciones de subrutinas OpenGL");}
                for(GLuint index=0;index<8;++index)gl_.BindBufferBase(storage_buffer,index,resource(dispatch.buffers[index],true).name);
                // Orphan the tiny uniform data store, like WRITE_DISCARD: a
                // preceding GPU pass retains its original parameters without
                // forcing the CPU to wait while overwriting that same store.
                gl_.BindBuffer(uniform_buffer,parameters_);gl_.BufferData(uniform_buffer,64,dispatch.constants.data(),dynamic_draw);gl_.BindBufferBase(uniform_buffer,8,parameters_);
                gl_.DispatchCompute(dispatch.groups[0],dispatch.groups[1],dispatch.groups[2]);
                // SSBO writes must be visible to the next shader AND to the
                // eventual untimed GetBufferSubData API readback.
                gl_.MemoryBarrier(storage_barrier|buffer_update_barrier);
            }
            if(measured)gl_.QueryCounter(queries_[1],timestamp);
            fence=gl_.FenceSync(sync_complete,0);require(fence!=nullptr,"No se pudo señalar la finalización GPU OpenGL.");
            gl_.Flush();gl_.check("Despachar secuencia compute OpenGL");const auto deadline=began+wait_budget;wait(fence,deadline,interrupted);
            gl_.DeleteSync(fence);fence=nullptr;gl_.UseProgram(0);
            if(interrupted)throw std::runtime_error("Prueba GPU OpenGL cancelada.");
            cancelled(stop_);
            if(!measured)return no_clock;
            for(const auto query:queries_){GLint available=GL_FALSE;unsigned polls=0;
                while(available==GL_FALSE){cancelled(stop_);gl_.GetQueryObjectiv(query,query_available,&available);
                    gl_.check("Consultar disponibilidad del reloj GPU OpenGL");require(Clock::now()<deadline,"El reloj GPU OpenGL no devolvió resultados en la espera acotada.");
                    if(available==GL_FALSE){++polls;if(polls<32||!SwitchToThread())YieldProcessor();}}}
            GlUInt64 start=0,end=0;gl_.GetQueryObjectui64v(queries_[0],query_result,&start);gl_.GetQueryObjectui64v(queries_[1],query_result,&end);
            if(gl_.errors())return no_clock;
            if(clock_bits_==64&&end<start)return no_clock;
            GlUInt64 elapsed=end-start;
            if(clock_bits_<64){const auto period=GlUInt64{1}<<clock_bits_;elapsed&=period-1;
                // CPU wall time is only an upper-bound validity check for a
                // wrapping GPU clock. It is NEVER returned as GPU duration.
                const auto wall=std::chrono::duration<long double,std::nano>(Clock::now()-began).count();
                if(wall>=static_cast<long double>(period))return no_clock;
            }
            const auto milliseconds=static_cast<double>(elapsed)/1000000.;
            return milliseconds>0&&std::isfinite(milliseconds)?milliseconds:no_clock;
        }catch(...){poisoned_=true;if(fence)gl_.DeleteSync(fence);gl_.UseProgram(0);gl_.Flush();throw;}
    }
};
}

std::unique_ptr<SmoCompute> create_smo_opengl_compute(const SmoComputeOptions& options,std::stop_token stop){
    return std::make_unique<OpenGlCompute>(options,stop);
}
}
