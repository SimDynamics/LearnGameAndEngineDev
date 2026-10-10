// Wasmtime 49.x C API bridge for Android ARM64.
// This code is compiled only when -Plgp.wasmtime=true is set.
// WAMR remains available for regression comparison.
#include <jni.h>
#include <android/log.h>

#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include <wasmtime.h>
#include <wasi.h>

namespace {

std::mutex s_runtime_mutex;
constexpr const char *kTraceTag = "LGP-Wasmtime";
void trace(const char *phase, const char *diagnostic_path = nullptr) {
    // Called often in the JNI path: keep logging and fsync completely disabled
    // unless the user explicitly requested a diagnostic build.
    if (!diagnostic_path || !diagnostic_path[0]) return;
    __android_log_print(ANDROID_LOG_INFO, kTraceTag, "%s", phase);
    // Persist small, source-free checkpoints outside logcat, which some
    // vendor devices do not expose for a crashed instrumentation process.
    const int fd = open(diagnostic_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return;
    (void)write(fd, phase, strlen(phase));
    (void)write(fd, "\n", 1);
    (void)fsync(fd);
    close(fd);
}
constexpr jint kFailure = -1;
constexpr jint kExecutionTimeout = -2;
constexpr size_t kMaxModuleBytes = 32 * 1024 * 1024;
constexpr int64_t kMemoryLimitBytes = 256 * 1024 * 1024;
constexpr int64_t kTableElementLimit = 1000000;
constexpr auto kExecutionDeadline = std::chrono::seconds(30);

// Epochs interrupt guest execution at Wasm checkpoints. Host WASI calls and
// Cranelift compilation are not interrupted by this execution deadline.
class ExecutionDeadline {
public:
    ExecutionDeadline(wasm_engine_t *engine, const char *diagnostic_path)
        : engine_(engine), diagnostic_path_(diagnostic_path) {}

    ~ExecutionDeadline() { finish(); }

    bool start() {
        const auto deadline = std::chrono::steady_clock::now() + kExecutionDeadline;
        try {
            worker_ = std::thread([this, deadline] {
                (void)pthread_setname_np(pthread_self(), "LGP-WasmLimit");
                std::unique_lock<std::mutex> lock(mutex_);
                if (changed_.wait_until(lock, deadline, [this] { return finished_; })) {
                    return;
                }
                expired_ = true;
                lock.unlock();
                // This is the only engine API used from the watchdog; Wasmtime
                // documents it as safe from any thread. Never touch the store,
                // context or JNI from here.
                wasmtime_engine_increment_epoch(engine_);
                trace("12a execution deadline reached after 30 seconds", diagnostic_path_);
            });
            return true;
        } catch (const std::system_error &) {
            return false;
        }
    }

    bool finish() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
        // join happens before the borrowed engine and diagnostic path can be
        // destroyed, including every early-return/error path in the scope.
        return expired_;
    }

    ExecutionDeadline(const ExecutionDeadline &) = delete;
    ExecutionDeadline &operator=(const ExecutionDeadline &) = delete;

private:
    wasm_engine_t *engine_;
    const char *diagnostic_path_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool finished_ = false;
    bool expired_ = false;
    std::thread worker_;
};

void report_execution_timeout(FILE *file) {
    if (!file) return;
    fprintf(file, "Wasmtime: límite de ejecución de 30 segundos alcanzado (epoch deadline).\n");
    fflush(file);
}

class JniString {
public:
    JniString(JNIEnv *env, jstring value)
        : env_(env), value_(value),
          data_(value ? env->GetStringUTFChars(value, nullptr) : nullptr) {}
    ~JniString() {
        if (data_) env_->ReleaseStringUTFChars(value_, data_);
    }
    const char *get() const { return data_; }

private:
    JNIEnv *env_;
    jstring value_;
    const char *data_;
};

void report_error(FILE *file, wasmtime_error_t *error) {
    if (!error) return;
    wasm_byte_vec_t message{};
    wasmtime_error_message(error, &message);
    if (file && message.data) {
        fprintf(file, "Wasmtime: %.*s\n",
                static_cast<int>(message.size), message.data);
        fflush(file);
    }
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
}

void report_trap(FILE *file, wasm_trap_t *trap) {
    if (!trap) return;
    wasm_byte_vec_t message{};
    wasm_trap_message(trap, &message);
    if (file && message.data) {
        fprintf(file, "WebAssembly trap: %.*s\n",
                static_cast<int>(message.size), message.data);
        fflush(file);
    }
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_learngraphicsprogramming_app_runtime_WasmtimeRuntime_execute(
        JNIEnv *env,
        jobject,
        jbyteArray wasm_bytes,
        jstring work_dir,
        jstring stdout_path,
        jstring stderr_path,
        jstring stdin_path,
        jstring phase_path) {
    std::lock_guard<std::mutex> lock(s_runtime_mutex);
    JniString working(env, work_dir);
    JniString out(env, stdout_path);
    JniString err(env, stderr_path);
    JniString in(env, stdin_path);
    JniString stage(env, phase_path);
    trace("00 JNI execute entered", stage.get());

    if (!wasm_bytes || !working.get() || !out.get() || !err.get() || !in.get()) {
        return kFailure;
    }

    // Never grant filesystem or networking access beyond the active job.
    FILE *diagnostics = fopen(err.get(), "w");
    if (!diagnostics) return kFailure;

    const jsize byte_count = env->GetArrayLength(wasm_bytes);
    if (byte_count < 1 || static_cast<size_t>(byte_count) > kMaxModuleBytes) {
        fprintf(diagnostics, "Wasmtime: módulo vacío o superior a 32 MiB.\n");
        fclose(diagnostics);
        return kFailure;
    }

    trace("01 WASM size accepted; acquiring bytes", stage.get());
    jbyte *bytes = env->GetByteArrayElements(wasm_bytes, nullptr);
    if (!bytes) {
        fclose(diagnostics);
        return kFailure;
    }

    jint code = kFailure;
    wasmtime_module_t *module = nullptr;
    wasmtime_store_t *store = nullptr;
    wasmtime_linker_t *linker = nullptr;

    // Wasmtime compiles the WebAssembly module and executes it inside
    // a new store, with limited memory and finite instruction fuel.
    trace("02 constructing wasm_config", stage.get());
    wasm_config_t *config = wasm_config_new();
    if (!config) {
        fprintf(diagnostics, "Wasmtime: error al crear wasm_config.\n");
        env->ReleaseByteArrayElements(wasm_bytes, bytes, JNI_ABORT);
        fclose(diagnostics);
        return kFailure;
    }
    // Keep the already-tested single-thread compilation setting for now.
    // WAMR and Wasmtime are isolated in distinct native build variants.
#ifdef WASMTIME_FEATURE_PARALLEL_COMPILATION
    wasmtime_config_parallel_compilation_set(config, false);
#endif
    // Cranelift is the validated optimizing backend for this variant.
    // WAMR remains the default when -Plgp.wasmtime=true is not supplied.
#ifdef WASMTIME_FEATURE_COMPILER
    trace("02a selecting Cranelift optimizing compiler", stage.get());
    wasmtime_config_strategy_set(config, WASMTIME_STRATEGY_CRANELIFT);
#else
    trace("02a compiler strategy unavailable; Wasmtime default applies", stage.get());
#endif
    wasmtime_config_consume_fuel_set(config, true);
    wasmtime_config_epoch_interruption_set(config, true);
    wasmtime_config_wasm_exceptions_set(config, true);
    // ART and Android's native libraries share process signal handlers.
    // Explicit Wasm trap/bounds checks keep unreachable and invalid memory
    // access inside the runtime instead of terminating the Android process.
    wasmtime_config_signals_based_traps_set(config, false);
    trace("03 constructing engine", stage.get());
    wasm_engine_t *engine = wasm_engine_new_with_config(config);
    trace("04 engine creation returned", stage.get());
    if (!engine) {
        fprintf(diagnostics, "Wasmtime: no se pudo crear el motor ARM64.\n");
    } else {
        do {
            trace("05 compiling full C++ WASM module", stage.get());
            wasmtime_error_t *error = wasmtime_module_new(
                engine, reinterpret_cast<uint8_t *>(bytes),
                static_cast<size_t>(byte_count), &module);
            if (error) {
                report_error(diagnostics, error);
                break;
            }

            trace("06 module compiled; constructing store", stage.get());
            store = wasmtime_store_new(engine, nullptr, nullptr);
            if (!store) {
                fprintf(diagnostics, "Wasmtime: no se pudo crear el almacén.\n");
                break;
            }
            // C++ WASI uses one linear memory, so this is also the total guest
            // linear-memory budget. Native/JIT allocations are not included.
            wasmtime_store_limiter(store, kMemoryLimitBytes, kTableElementLimit, 1, 2, 1);
            wasmtime_context_t *context = wasmtime_store_context(store);
            error = wasmtime_context_set_fuel(context, 1000000000ULL);
            if (error) {
                report_error(diagnostics, error);
                break;
            }

            trace("07 fuel/store ready; configuring WASI", stage.get());
            wasi_config_t *wasi = wasi_config_new();
            if (!wasi) {
                fprintf(diagnostics, "Wasmtime: error al crear wasi_config.\n");
                break;
            }
            if (!wasi_config_set_stdin_file(wasi, in.get()) ||
                !wasi_config_set_stdout_file(wasi, out.get()) ||
                !wasi_config_set_stderr_file(wasi, err.get()) ||
                !wasi_config_preopen_dir(wasi, working.get(), ".", true)) {
                fprintf(diagnostics, "Wasmtime: no se pudieron preparar los archivos WASI.\n");
                // set_wasi takes ownership; it must be called only on success.
                wasi_config_delete(wasi);
                break;
            }

            // Close our diagnostics handle first because Wasmtime now owns
            // stdout/stderr file writers for the WASI guest.
            fclose(diagnostics);
            diagnostics = nullptr;

            trace("08 applying WASI config", stage.get());
            error = wasmtime_context_set_wasi(context, wasi);
            // Ownership of wasi is transferred even if this call fails.
            if (error) {
                diagnostics = fopen(err.get(), "a");
                report_error(diagnostics, error);
                break;
            }

            trace("09 creating WASI linker", stage.get());
            linker = wasmtime_linker_new(engine);
            error = wasmtime_linker_define_wasi(linker);
            if (error) {
                diagnostics = fopen(err.get(), "a");
                report_error(diagnostics, error);
                break;
            }

            // An instance can execute its own Wasm start function while being
            // created; arm the same deadline before that and keep it through
            // the exported WASI _start call.
            wasmtime_context_set_epoch_deadline(context, 1);
            ExecutionDeadline deadline(engine, stage.get());
            if (!deadline.start()) {
                diagnostics = fopen(err.get(), "a");
                if (diagnostics) {
                    fprintf(diagnostics, "Wasmtime: no se pudo activar el límite de ejecución.\n");
                }
                break;
            }
            trace("10a execution deadline armed for 30 seconds", stage.get());
            trace("10 instantiating WASM module", stage.get());
            wasmtime_instance_t instance{};
            wasm_trap_t *trap = nullptr;
            error = wasmtime_linker_instantiate(linker, context, module, &instance, &trap);
            if (error || trap) {
                const bool timed_out = deadline.finish();
                diagnostics = fopen(err.get(), "a");
                if (timed_out) {
                    code = kExecutionTimeout;
                    report_execution_timeout(diagnostics);
                }
                report_error(diagnostics, error);
                report_trap(diagnostics, trap);
                break;
            }

            trace("11 WASM instantiated; resolving _start", stage.get());
            wasmtime_extern_t function{};
            if (!wasmtime_instance_export_get(context, &instance, "_start", 6, &function) ||
                function.kind != WASMTIME_EXTERN_FUNC) {
                diagnostics = fopen(err.get(), "a");
                if (diagnostics) fprintf(diagnostics, "Wasmtime: falta la función WASI _start.\n");
                break;
            }

            trace("12 calling _start", stage.get());
            error = wasmtime_func_call(context, &function.of.func,
                                      nullptr, 0, nullptr, 0, &trap);
            const bool timed_out = deadline.finish();
            trace("13 _start call returned", stage.get());
            trace("13a execution deadline watchdog joined", stage.get());
            wasmtime_extern_delete(&function);
            if (timed_out) {
                code = kExecutionTimeout;
                diagnostics = fopen(err.get(), "a");
                report_execution_timeout(diagnostics);
                report_error(diagnostics, error);
                report_trap(diagnostics, trap);
                break;
            }
            if (error) {
                int exit_status = 0;
                if (wasmtime_error_exit_status(error, &exit_status)) {
                    code = exit_status;
                    wasmtime_error_delete(error);
                    break;
                }
                diagnostics = fopen(err.get(), "a");
                report_error(diagnostics, error);
                break;
            }
            if (trap) {
                diagnostics = fopen(err.get(), "a");
                report_trap(diagnostics, trap);
                break;
            }
            code = 0;
        } while (false);
    }

    trace("14 cleanup started", stage.get());
    if (linker) wasmtime_linker_delete(linker);
    if (store) wasmtime_store_delete(store);
    if (module) wasmtime_module_delete(module);
    if (engine) wasm_engine_delete(engine);
    env->ReleaseByteArrayElements(wasm_bytes, bytes, JNI_ABORT);
    if (diagnostics) fclose(diagnostics);
    trace("15 JNI execute completed", stage.get());
    return code;
}
