#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <jni.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static_assert(__cplusplus == 202302L && _MSVC_LANG == 202302L,
    "The Windows JVM launcher must use the same exact C++23 profile as the application.");

namespace {
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if (!length) throw std::runtime_error("Invalid UTF-16 runtime argument");
    std::string result(static_cast<std::size_t>(length),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr);
    return result;
}
}

// One child process hosts one JVM. Loading the installed Windows C++ runtime
// first prevents a JDK's older private msvcp140.dll from breaking current ORT.
// Nothing is copied into, installed in, or changed in the JDK or Windows.
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 4) throw std::runtime_error("Expected JVM DLL, classpath and worker main class");
        const std::filesystem::path jvm_path(argv[1]);
        if (!jvm_path.is_absolute() || !std::filesystem::is_regular_file(jvm_path))
            throw std::runtime_error("Configured x64 JVM DLL is missing");
        if (!LoadLibraryExW(L"msvcp140.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32))
            throw std::runtime_error("The installed Windows Microsoft C++ runtime is unavailable");
        const auto directory = jvm_path.parent_path().parent_path();
        const auto added_directory = AddDllDirectory(directory.c_str());
        if (!added_directory) throw std::runtime_error("Cannot set process-local JVM dependency directory");
        const auto library = LoadLibraryExW(jvm_path.c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) throw std::runtime_error("Cannot load the configured x64 JVM");
        using CreateVm = jint (JNICALL*)(JavaVM**,void**,void*);
        const auto create_vm = reinterpret_cast<CreateVm>(GetProcAddress(library,"JNI_CreateJavaVM"));
        if (!create_vm) throw std::runtime_error("The configured runtime does not expose JNI Invocation");
        std::vector<std::string> options{
            "-Djava.class.path=" + utf8(argv[2]),"-Dfile.encoding=UTF-8",
            "-Dstdout.encoding=UTF-8","-Dstderr.encoding=UTF-8","-Xmx2048m",
            "--enable-native-access=ALL-UNNAMED"};
        std::vector<JavaVMOption> native_options;
        for (auto& option : options) native_options.push_back({option.data(),nullptr});
        JavaVMInitArgs settings{}; settings.version = JNI_VERSION_1_8;
        settings.nOptions = static_cast<jint>(native_options.size()); settings.options = native_options.data();
        settings.ignoreUnrecognized = JNI_FALSE;
        JavaVM* vm = nullptr; JNIEnv* environment = nullptr;
        if (create_vm(&vm,reinterpret_cast<void**>(&environment),&settings) != JNI_OK || !vm || !environment)
            throw std::runtime_error("Cannot start the shared Kokoro frontend JVM");
        auto main_name = utf8(argv[3]);
        for (auto& symbol : main_name) if (symbol == '.') symbol = '/';
        const auto main_class = environment->FindClass(main_name.c_str());
        const auto main_method = main_class ? environment->GetStaticMethodID(main_class,"main","([Ljava/lang/String;)V") : nullptr;
        const auto string_class = environment->FindClass("java/lang/String");
        const auto arguments = string_class ? environment->NewObjectArray(argc - 4,string_class,nullptr) : nullptr;
        if (main_class && main_method && arguments && !environment->ExceptionCheck()) {
            for (int index = 4; index < argc; ++index) {
                const std::wstring value(argv[index]);
                const auto string = environment->NewString(reinterpret_cast<const jchar*>(value.data()),static_cast<jsize>(value.size()));
                if (!string) break;
                environment->SetObjectArrayElement(arguments,index - 4,string);
                environment->DeleteLocalRef(string);
            }
            if (!environment->ExceptionCheck()) environment->CallStaticVoidMethod(main_class,main_method,arguments);
        }
        const bool failed = environment->ExceptionCheck() || !main_class || !main_method || !arguments;
        if (failed) environment->ExceptionDescribe();
        if (arguments) environment->DeleteLocalRef(arguments);
        if (string_class) environment->DeleteLocalRef(string_class);
        if (main_class) environment->DeleteLocalRef(main_class);
        vm->DestroyJavaVM();
        RemoveDllDirectory(added_directory);
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Kokoro Windows runtime: " << error.what() << '\n';
        return 2;
    }
}
