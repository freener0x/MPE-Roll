// A CLAP plugin file that crashes (or, built with MPEROLL_TEST_PLUGIN_HANGS, hangs) as soon as a host
// starts loading it. The harness checks that MPE Roll looks at such files in its scanner process and
// survives them.

#include <clap/clap.h>

#if MPEROLL_TEST_PLUGIN_HANGS
 #include <chrono>
 #include <thread>
#endif

namespace
{

bool init (const char*)
{
   #if MPEROLL_TEST_PLUGIN_HANGS
    for (;;)
        std::this_thread::sleep_for (std::chrono::seconds (1));
   #else
    volatile int* volatile nowhere = nullptr;
    *nowhere = 1;
    return false;
   #endif
}

void deinit() {}

const void* getFactory (const char*)
{
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, init, deinit, getFactory };
