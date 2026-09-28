#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

#include "wasm_export.h"

namespace {

constexpr uint32_t kStackSize = 1024 * 1024;
constexpr uint32_t kHeapSize = 16 * 1024 * 1024;
constexpr int kRuntimeFailure = -1;

std::mutex runtime_mutex;

class UtfChars {
public:
    UtfChars(JNIEnv *environment, jstring value)
        : env(environment), source(value), chars(value ? env->GetStringUTFChars(value, nullptr) : nullptr) {}

    ~UtfChars() {
        if (chars != nullptr) {
            env->ReleaseStringUTFChars(source, chars);
        }
    }

    const char *get() const { return chars; }

private:
    JNIEnv *env;
    jstring source;
    const char *chars;
};

void write_error(int fd, const char *message) {
    if (fd >= 0 && message != nullptr) {
        dprintf(fd, "%s\n", message);
    }
}

}  // namespace

extern "C"
JNIEXPORT jint JNICALL
Java_com_learngraphicsprogramming_app_runtime_WasiRuntime_execute(
        JNIEnv *env,
        jobject,
        jbyteArray wasm_bytes,
        jstring working_directory,
        jstring stdout_path,
        jstring stderr_path) {
    std::lock_guard<std::mutex> lock(runtime_mutex);

    UtfChars work_dir(env, working_directory);
    UtfChars stdout_file(env, stdout_path);
    UtfChars stderr_file(env, stderr_path);
    if (wasm_bytes == nullptr || work_dir.get() == nullptr ||
        stdout_file.get() == nullptr || stderr_file.get() == nullptr) {
        return kRuntimeFailure;
    }

    const int stdout_fd = open(stdout_file.get(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    const int stderr_fd = open(stderr_file.get(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (stdout_fd < 0 || stderr_fd < 0) {
        if (stdout_fd >= 0) close(stdout_fd);
        if (stderr_fd >= 0) close(stderr_fd);
        return kRuntimeFailure;
    }

    int result = kRuntimeFailure;
    RuntimeInitArgs init_args{};
    init_args.mem_alloc_type = Alloc_With_System_Allocator;

    if (!wasm_runtime_full_init(&init_args)) {
        write_error(stderr_fd, "WAMR no pudo inicializar el entorno de ejecución.");
        close(stdout_fd);
        close(stderr_fd);
        return result;
    }

    wasm_module_t module = nullptr;
    wasm_module_inst_t instance = nullptr;
    jbyte *bytes = env->GetByteArrayElements(wasm_bytes, nullptr);
    const jsize byte_count = env->GetArrayLength(wasm_bytes);
    char error_buffer[512]{};

    if (bytes == nullptr || byte_count <= 0) {
        write_error(stderr_fd, "El módulo WebAssembly está vacío.");
        goto cleanup;
    }

    module = wasm_runtime_load(
            reinterpret_cast<uint8_t *>(bytes),
            static_cast<uint32_t>(byte_count),
            error_buffer,
            sizeof(error_buffer));
    if (module == nullptr) {
        dprintf(stderr_fd, "WAMR no pudo cargar el módulo: %s\n", error_buffer);
        goto cleanup;
    }

    {
        const char *directories[] = {work_dir.get()};
        wasm_runtime_set_wasi_args_ex(
                module,
                directories,
                1,
                nullptr,
                0,
                nullptr,
                0,
                nullptr,
                0,
                -1,
                stdout_fd,
                stderr_fd);
    }

    instance = wasm_runtime_instantiate(
            module,
            kStackSize,
            kHeapSize,
            error_buffer,
            sizeof(error_buffer));
    if (instance == nullptr) {
        dprintf(stderr_fd, "WAMR no pudo crear el programa: %s\n", error_buffer);
        goto cleanup;
    }

    if (!wasm_application_execute_main(instance, 0, nullptr)) {
        const char *exception = wasm_runtime_get_exception(instance);
        dprintf(
                stderr_fd,
                "La ejecución WebAssembly falló: %s\n",
                exception != nullptr ? exception : "error desconocido");
        goto cleanup;
    }

    result = wasm_runtime_get_wasi_exit_code(instance);

cleanup:
    if (instance != nullptr) {
        wasm_runtime_deinstantiate(instance);
    }
    if (module != nullptr) {
        wasm_runtime_unload(module);
    }
    if (bytes != nullptr) {
        env->ReleaseByteArrayElements(wasm_bytes, bytes, JNI_ABORT);
    }
    wasm_runtime_destroy();
    close(stdout_fd);
    close(stderr_fd);
    return result;
}
