#ifdef __vita__

// Vita3K v0.2.1 rejects sceGxmCreateContext with SCE_GXM_ERROR_ALREADY_INITIALIZED
// (its emuenv.gxm.immediate_context is already non-zero by the time vitaGL gets
// here). vitaGL then NULL-derefs the uninitialized output pointer.
//
// A C definition named sceGxmCreateContext does not intercept the call because
// vita-elf-create rewrites it as a NID import that the Vita kernel resolves at
// load time to the real syscall stub. Use the linker --wrap=sceGxmCreateContext
// flag (set in CMakeLists for the VITA target) so the call goes through
// __wrap_sceGxmCreateContext below, which can swallow the Vita3K error and
// hand vitaGL a non-NULL dummy context.

#include <psp2/gxm.h>

static char vita3k_dummy_context[4096] __attribute__((aligned(16)));

extern int __real_sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **context);

int __wrap_sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **context) {
    int rc = __real_sceGxmCreateContext(params, context);
    if (rc != 0 && context) {
        *context = (SceGxmContext *)vita3k_dummy_context;
        return 0;
    }
    return rc;
}

#endif
