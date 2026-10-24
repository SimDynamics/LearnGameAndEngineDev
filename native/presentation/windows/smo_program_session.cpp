#include "presentation/windows/smo_program_session.hpp"
#include "presentation/windows/smo_native_render.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace lgaed::presentation {
namespace {
// The worker/lifecycle is shared. Only this numerical backend changes; the
// native path never evaluates through the F12 interpreter or reads geometry
// back to the CPU. SmoProgramImage below is only the common pixel DTO.
struct SessionFrame {std::uint32_t vertices=0,faces=0;SmoProgramImage image;};
class SessionProgram {
    std::optional<SmoGpuProgram> reference_;
    std::unique_ptr<SmoProgramRenderer> reference_renderer_;
    std::optional<SmoNativeGpuProgram> native_;
    std::unique_ptr<SmoNativeRenderer> native_renderer_;
public:
    SessionProgram(std::string_view source,SmoComputeApi api,const SmoComputeOptions& device,
                   std::uint32_t width,std::uint32_t height,SmoProgramBackend backend,std::stop_token stop){
        if(backend==SmoProgramBackend::compiled_binary32){
            SmoNativeGpuOptions options;options.api=api;options.device=device;
            options.scalar_profile=smo::NativeScalarProfile::ordinary_binary32;
            native_.emplace(SmoNativeGpuProgram::compile(source,options,stop));
            native_renderer_=std::make_unique<SmoNativeRenderer>(native_->compute(),width,height);
        }else{
            reference_.emplace(SmoGpuProgram::compile(source,api,device,stop));
            reference_renderer_=std::make_unique<SmoProgramRenderer>(reference_->compute(),width,height);
        }
    }
    SmoComputeInfo info()const{return native_?native_->info():reference_->info();}
    SmoProgramBackend backend()const{return native_?SmoProgramBackend::compiled_binary32:SmoProgramBackend::experimental_f12;}
    std::pair<std::uint32_t,std::uint32_t> counts()const{
        if(native_){const auto view=native_->frame_view();return {view.vertex_count,view.face_count};}
        const auto frame=reference_->current_frame();return {frame.vertex_count,frame.face_count};
    }
    SessionFrame render(){SessionFrame result;
        if(native_){const auto view=native_->frame_view();auto image=native_renderer_->render(view);
            if(image.diagnostic)throw std::runtime_error("La representación nativa produjo un diagnóstico: "+std::to_string(image.diagnostic));
            result.vertices=view.vertex_count;result.faces=view.face_count;
            result.image.width=image.width;result.image.height=image.height;result.image.colored_pixels=image.colored_pixels;
            result.image.pixel_hash=image.pixel_hash;result.image.pixels=std::move(image.pixels);
        }else{const auto frame=reference_->current_frame();result.vertices=frame.vertex_count;result.faces=frame.face_count;result.image=reference_renderer_->render(frame);
            if(result.image.diagnostic&smo_d12_fatal_mask)throw std::runtime_error("La representación gráfica F12 produjo un diagnóstico: "+std::to_string(result.image.diagnostic));}
        return result;
    }
    SessionFrame step(std::uint64_t nanoseconds){
        if(native_){const std::array<std::uint64_t,1> ticks{nanoseconds};const auto execution=native_->step_elapsed(ticks,false);
            if(!execution.committed){for(const auto& error:execution.diagnostics)if(error.code!=smo::NativeError::none)
                throw std::runtime_error("Programa nativo: diagnóstico "+std::to_string(static_cast<std::uint32_t>(error.code))+" en "+std::to_string(error.line)+":"+std::to_string(error.column)+". Se conserva la posición anterior.");
                throw std::runtime_error("La GPU no confirmó la actualización nativa.");}
        }else (void)reference_->step_elapsed(nanoseconds);
        return render();
    }
};
}
struct SmoProgramSession::Impl {
    using Clock=std::chrono::steady_clock;
    struct Request {std::uint64_t generation=0;std::optional<std::string> source;std::stop_source cancel;};
    SmoComputeApi api;SmoComputeOptions device;std::uint32_t width,height;std::function<void()> changed;SmoProgramBackend backend;
    std::mutex mutex,execution;std::condition_variable_any wake;
    std::optional<Request> request;std::optional<SmoProgramUpdate> update;
    std::stop_source current_cancel;std::uint64_t generation=0,clock_revision=0;
    bool active=false,shown=true,ready=false,compiling=false,in_frame=false;
    std::jthread worker;
    Impl(SmoComputeApi a,SmoComputeOptions d,std::uint32_t w,std::uint32_t h,std::function<void()> c,SmoProgramBackend b)
        :api(a),device(std::move(d)),width(w),height(h),changed(std::move(c)),backend(b),worker([this](std::stop_token stop){run(stop);}){}
    ~Impl(){worker.request_stop();{std::lock_guard lock(mutex);current_cancel.request_stop();}wake.notify_all();worker.join();}
    void publish(SmoProgramUpdate value) {
        {std::lock_guard lock(mutex);if(value.generation!=generation)return;ready=value.ready;update=std::move(value);}
        if(changed)changed();
    }
    bool idle_locked()const {return !compiling&&!in_frame&&!request;}
    void notify_idle() {
        bool notify=false;{std::lock_guard lock(mutex);notify=idle_locked();}
        if(notify&&changed)changed();
    }
    void submit(std::optional<std::string> text) {
        {std::lock_guard lock(mutex);current_cancel.request_stop();current_cancel=std::stop_source{};
            ++generation;active=text.has_value();ready=false;update.reset();request=Request{generation,std::move(text),current_cancel};}
        wake.notify_all();
    }
    void run(std::stop_token stop) {
        std::unique_ptr<SessionProgram> program;
        std::uint64_t live=0,frames=0,last_clock_revision=0;auto last=Clock::now();
        std::stop_callback shutdown(stop,[this]{std::lock_guard lock(mutex);current_cancel.request_stop();wake.notify_all();});
        while(!stop.stop_requested()) {
            std::optional<Request> next;
            {std::unique_lock lock(mutex);wake.wait(lock,stop,[&]{return request.has_value()||(program&&active&&shown);});
                if(stop.stop_requested())break;if(request){next=std::move(request);request.reset();compiling=true;}}
            if(next) {
                program.reset();live=next->generation;frames=0;
                if(!next->source){{std::lock_guard lock(mutex);compiling=false;}notify_idle();continue;}
                try {
                    const auto began=Clock::now();
                    program=std::make_unique<SessionProgram>(*next->source,api,device,width,height,backend,next->cancel.get_token());
                    auto frame=program->render();
                    SmoProgramUpdate value;value.backend=program->backend();value.generation=live;value.frames=++frames;value.vertices=frame.vertices;value.faces=frame.faces;
                    value.ready=true;value.device=program->info();value.image=std::move(frame.image);value.elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
                    publish(std::move(value));
                }catch(const std::exception& error){program.reset();SmoProgramUpdate value;value.generation=live;value.error=error.what();publish(std::move(value));}
                {std::lock_guard lock(mutex);compiling=false;}last=Clock::now();notify_idle();continue;
            }
            // Pause synchronizes at a frame boundary. Source invalidation can
            // return immediately: the generation check suppresses its output.
            std::unique_lock frame_lock(execution);
            bool reset_clock=false;
            {std::lock_guard lock(mutex);if(!program||!active||!shown||request){last=Clock::now();continue;}
                reset_clock=last_clock_revision!=clock_revision;last_clock_revision=clock_revision;in_frame=true;}
            try {
                const auto began=Clock::now();const auto ticks=reset_clock?0:std::chrono::duration_cast<std::chrono::nanoseconds>(began-last).count();last=began;
                auto frame=program->step(static_cast<std::uint64_t>(ticks));
                SmoProgramUpdate value;value.backend=program->backend();value.generation=live;value.frames=++frames;value.vertices=frame.vertices;value.faces=frame.faces;
                value.ready=true;value.device=program->info();value.image=std::move(frame.image);value.elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-began).count();publish(std::move(value));
            }catch(const std::exception& error){
                // A numerical/semantic failure already rolled the VM back to
                // its committed bank. Preserve it and the renderer for an
                // explicit resume/reset; do not turn that failure into a
                // destructive recompilation or publish it for obsolete source.
                bool current=false;{std::lock_guard lock(mutex);current=live==generation;if(current){active=false;++clock_revision;}}
                if(current){const auto [vertices,faces]=program->counts();SmoProgramUpdate value;value.backend=program->backend();value.generation=live;value.frames=frames;
                    value.vertices=vertices;value.faces=faces;value.device=program->info();value.ready=true;value.error=error.what();publish(std::move(value));}
            }
            {std::lock_guard lock(mutex);in_frame=false;}notify_idle();
            frame_lock.unlock();
            std::unique_lock lock(mutex);wake.wait_for(lock,stop,std::chrono::milliseconds(16),[&]{return request.has_value()||!active||!shown;});
            if(!active||!shown)last=Clock::now();
        }
        // Contexts and all their GPU resources die on their creation thread.
        program.reset();
    }
};
SmoProgramSession::SmoProgramSession(SmoComputeApi a,SmoComputeOptions d,std::uint32_t w,std::uint32_t h,std::function<void()> c,SmoProgramBackend b)
    :impl_(std::make_unique<Impl>(a,std::move(d),w,h,std::move(c),b)){}
SmoProgramSession::~SmoProgramSession()=default;
void SmoProgramSession::execute(std::string text){impl_->submit(std::move(text));}
void SmoProgramSession::clear(){impl_->submit(std::nullopt);}
void SmoProgramSession::running(bool value,bool synchronize){
    bool wait=false;{std::lock_guard lock(impl_->mutex);if(impl_->active!=value)++impl_->clock_revision;impl_->active=value;wait=synchronize&&!value&&impl_->ready&&!impl_->compiling;}
    impl_->wake.notify_all();if(wait){std::lock_guard boundary(impl_->execution);}
}
void SmoProgramSession::visible(bool value){
    {std::lock_guard lock(impl_->mutex);if(impl_->shown!=value)++impl_->clock_revision;impl_->shown=value;}impl_->wake.notify_all();
}
bool SmoProgramSession::idle()const{std::lock_guard lock(impl_->mutex);return impl_->idle_locked();}
std::optional<SmoProgramUpdate> SmoProgramSession::take(){std::lock_guard lock(impl_->mutex);auto result=std::move(impl_->update);impl_->update.reset();return result;}
} //namespace lgaed::presentation
