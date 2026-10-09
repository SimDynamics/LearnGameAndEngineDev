// Wasmtime 49.x C API bridge for Android ARM64.
// This code is compiled only when -Plgp.wasmtime=true is set.
// WAMR remains available for regression comparison.
#include <jni.h>
#include <android/log.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <mutex>
#include <string>

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
constexpr size_t kMaxModuleBytes = 32 * 1024 * 1024;
constexpr int64_t kMemoryLimitBytes = 256 * 1024 * 1024;

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
    wasmtime_config_wasm_exceptions_set(config, true);
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
            wasmtime_store_limiter(store, kMemoryLimitBytes, -1, 1, 2, 2);
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

            trace("10 instantiating WASM module", stage.get());
            wasmtime_instance_t instance{};
            wasm_trap_t *trap = nullptr;
            error = wasmtime_linker_instantiate(linker, context, module, &instance, &trap);
            if (error || trap) {
                diagnostics = fopen(err.get(), "a");
                report_error(diagnostics, error);
                report_trap(diagnostics, trap);
                break;
            }

            trace("11 WASM instantiated; resolving _start", stage.get());
            wasmtime_extern_t function{};
            if (!wasmtime_instance_export_get(context, &instance, "_start", 6, &function) ||
                function.kind != WASMTIME_EXTERN_FUNC) {
                diagnostics = fopen(err.get(), "a");
                fprintf(diagnostics, "Wasmtime: falta la función WASI _start.\n");
                break;
            }

            trace("12 calling _start", stage.get());
            error = wasmtime_func_call(context, &function.of.func,
                                      nullptr, 0, nullptr, 0, &trap);
            trace("13 _start call returned", stage.get());
            wasmtime_extern_delete(&function);
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
