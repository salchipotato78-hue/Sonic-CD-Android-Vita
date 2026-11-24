#include "utils/init.h"
#include "utils/glutil.h"
#if defined(GAME_SONICCD)
#include "soniccd/driver.h"
#define game_init   soniccd_init
#define game_frame  soniccd_frame
#define game_shutdown soniccd_shutdown
#define game_key    soniccd_key
#define game_touch  soniccd_touch
#define GAME_DATA_FILE DATA_PATH "Data.rsdk"
#else
#include "sonic1/driver.h"
#define game_init   sonic1_init
#define game_frame  sonic1_frame
#define game_shutdown sonic1_shutdown
#define game_key    sonic1_key
#define game_touch  sonic1_touch
#define GAME_DATA_FILE DATA_PATH "Data.rsdk.xmf"
#endif

#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#ifndef NDK_PORT
#include "reimpl/controls.h"
#endif

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;

int main() {
    /* Carga, relocación, resolución de imports y init del .so (ver utils/init.c). */
    soloader_init_all();

    gl_init();

    if (game_init(&so_mod, GAME_DATA_FILE, DATA_PATH "SData.bin") < 0) {
        /* TODO: mostrar error al usuario con utils/dialog.h */
        sceKernelExitDeleteThread(0);
    }

    while (1) {
#ifndef NDK_PORT
        /* Sin esto no se leen botones ni touch: controls_poll() nunca se llamaba. */
        controls_poll();
#endif
        game_frame();
        gl_swap();
    }

    game_shutdown();
    sceKernelExitDeleteThread(0);
}

#ifndef NDK_PORT
void controls_handler_key(int32_t keycode, ControlsAction action) {
    game_key(keycode, (int32_t)action); /* keycode se reinterpreta como índice; ver docs/SYMBOLS.md */
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    game_touch(id, x, y, (int32_t)action);
}

/* Stick izquierdo -> D-pad. Envía DOWN/UP solo cuando cambia la dirección. */
static void analog_dir(int32_t keycode, int want, int *state) {
    if (want != *state) {
        *state = want;
        game_key(keycode, want ? CONTROLS_ACTION_DOWN : CONTROLS_ACTION_UP);
    }
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    static int st_up, st_down, st_left, st_right;
    (void)action; /* el estado se calcula en cada lectura, no por evento */
    if (which != CONTROLS_STICK_LEFT)
        return; /* el stick derecho no se usa en el juego */

    const float th = 0.5f;
    analog_dir(AKEYCODE_DPAD_UP,    y < -th, &st_up);
    analog_dir(AKEYCODE_DPAD_DOWN,  y >  th, &st_down);
    analog_dir(AKEYCODE_DPAD_LEFT,  x < -th, &st_left);
    analog_dir(AKEYCODE_DPAD_RIGHT, x >  th, &st_right);
}
#endif
