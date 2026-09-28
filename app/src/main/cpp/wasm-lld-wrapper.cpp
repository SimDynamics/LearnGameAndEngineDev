#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv) {
    const char *core = std::getenv("LGP_LLD_CORE");
    if (core == nullptr || core[0] == '\0') {
        std::fputs("LGP wasm-ld: falta LGP_LLD_CORE\n", stderr);
        return 127;
    }

    std::vector<char *> forwarded;
    forwarded.reserve(static_cast<size_t>(argc) + 1);
    forwarded.push_back(const_cast<char *>("wasm-ld"));
    for (int index = 1; index < argc; ++index) {
        forwarded.push_back(argv[index]);
    }
    forwarded.push_back(nullptr);

    void *handle = dlopen(core, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::fprintf(stderr, "LGP wasm-ld: dlopen: %s\n", dlerror());
        return 127;
    }

    using LldMain = int (*)(int, char **);
    dlerror();
    auto *entry = reinterpret_cast<LldMain>(dlsym(handle, "main"));
    const char *error = dlerror();
    if (error != nullptr || entry == nullptr) {
        std::fprintf(
                stderr,
                "LGP wasm-ld: dlsym(main): %s\n",
                error != nullptr ? error : "símbolo ausente");
        dlclose(handle);
        return 127;
    }

    const int result = entry(argc, forwarded.data());
    dlclose(handle);
    return result;
}
