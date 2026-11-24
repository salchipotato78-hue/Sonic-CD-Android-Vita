#pragma once
#include <so_util/so_util.h>
#include <stdint.h>

/* Driver que reproduce, desde C, las llamadas JNI que haría la activity de Java
 * sobre libRetroEngineJNI.so de Sonic CD. Ver docs/SYMBOLS_SONICCD.md. */
int  soniccd_init(so_module *mod, const char *data_dir, const char *save_file);
void soniccd_frame(void);
void soniccd_shutdown(void);
void soniccd_key(int32_t keycode, int32_t action);
void soniccd_touch(int32_t id, float x, float y, int32_t action);
