#include <jni.h>
#include <sched.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

namespace {
void fail(JNIEnv* env, const std::string& message) {
    jclass type = env->FindClass("java/lang/IllegalStateException");
    if (type != nullptr) env->ThrowNew(type, message.c_str());
}

bool current_mask(JNIEnv* env, cpu_set_t& mask) {
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) != 0) {
        const int error = errno;
        fail(env, "sched_getaffinity(current thread): " + std::string(std::strerror(error)));
        return false;
    }
    if (CPU_COUNT(&mask) == 0) {
        fail(env, "Android returned an empty current-thread CPU mask");
        return false;
    }
    return true;
}
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_learngraphicsprogramming_app_narration_NarrationCpuBudget_nativeCurrentTid(JNIEnv*, jobject) {
    return static_cast<jlong>(gettid());
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_learngraphicsprogramming_app_narration_NarrationCpuBudget_nativeAllowedCpus(JNIEnv* env, jobject) {
    cpu_set_t mask;
    if (!current_mask(env, mask)) return nullptr;
    std::vector<jint> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &mask)) cpus.push_back(static_cast<jint>(cpu));
    }
    jintArray result = env->NewIntArray(static_cast<jsize>(cpus.size()));
    if (result != nullptr) env->SetIntArrayRegion(result, 0, static_cast<jsize>(cpus.size()), cpus.data());
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_learngraphicsprogramming_app_narration_NarrationCpuBudget_nativeSetAllowedCpus(
    JNIEnv* env, jobject, jintArray cpus) {
    if (cpus == nullptr) {
        fail(env, "A CPU affinity mask is required");
        return;
    }
    const jsize count = env->GetArrayLength(cpus);
    if (count <= 0 || count > CPU_SETSIZE) {
        fail(env, "CPU affinity must contain supported CPU identifiers");
        return;
    }
    std::vector<jint> requested(static_cast<size_t>(count));
    env->GetIntArrayRegion(cpus, 0, count, requested.data());
    if (env->ExceptionCheck()) return;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (jint cpu : requested) {
        if (cpu < 0 || cpu >= CPU_SETSIZE) {
            fail(env, "CPU identifier exceeds the supported native affinity mask");
            return;
        }
        CPU_SET(cpu, &mask);
    }
    // pid zero always targets only this calling thread. Never use the application pid here.
    if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
        const int error = errno;
        fail(env, "sched_setaffinity(current thread): " + std::string(std::strerror(error)));
    }
}
