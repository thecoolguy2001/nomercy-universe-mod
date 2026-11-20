/* Scripted controller-1 input plugin for mupen64plus. Buttons come from the
 * headless video plugin (nmdbg_buttons), which owns the frame-timed script.
 * Env NMDBG_VIDEO_SO = path of that video plugin. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_plugin.h"
#include "m64p_types.h"

#define EXPORT __attribute__((visibility("default")))
#define CALL

static uint32_t (*get_buttons)(void);
static int (*get_sx)(void), (*get_sy)(void);

static void bind(void)
{
    void *h;
    const char *so = getenv("NMDBG_VIDEO_SO");
    if (get_buttons || !so) return;
    h = dlopen(so, RTLD_NOW | RTLD_NOLOAD);
    if (!h) return;
    get_buttons = (uint32_t (*)(void))dlsym(h, "nmdbg_buttons");
    get_sx = (int (*)(void))dlsym(h, "nmdbg_stick_x");
    get_sy = (int (*)(void))dlsym(h, "nmdbg_stick_y");
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle h, void *ctx, void (*cb)(void *, int, const char *))
{
    (void)h; (void)ctx; (void)cb;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void) { return M64ERR_SUCCESS; }

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *ver, int *api,
                                        const char **name, int *caps)
{
    if (type) *type = M64PLUGIN_INPUT;
    if (ver) *ver = 0x010000;
    if (api) *api = 0x020100;
    if (name) *name = "nmdbg scripted input";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

EXPORT void CALL InitiateControllers(CONTROL_INFO info)
{
    int i;
    for (i = 0; i < 4; i++) {
        info.Controls[i].Present = i == 0;
        info.Controls[i].RawData = 0;
        info.Controls[i].Plugin = PLUGIN_NONE;
    }
}

EXPORT void CALL GetKeys(int ctl, BUTTONS *keys)
{
    bind();
    keys->Value = 0;
    if (ctl != 0 || !get_buttons) return;
    keys->Value = get_buttons() & 0xFFFF;
    keys->X_AXIS = get_sx();
    keys->Y_AXIS = get_sy();
}

EXPORT void CALL ControllerCommand(int ctl, unsigned char *cmd) { (void)ctl; (void)cmd; }
EXPORT void CALL ReadController(int ctl, unsigned char *cmd) { (void)ctl; (void)cmd; }
EXPORT int  CALL RomOpen(void) { return 1; }
EXPORT void CALL RomClosed(void) {}
EXPORT void CALL SDL_KeyDown(int mod, int sym) { (void)mod; (void)sym; }
EXPORT void CALL SDL_KeyUp(int mod, int sym) { (void)mod; (void)sym; }
EXPORT void CALL RenderCallback(void) {}
