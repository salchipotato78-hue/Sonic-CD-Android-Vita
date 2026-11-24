#pragma once
#include <so_util/so_util.h>
#include <stdint.h>

/* Driver que reproduce, desde C, las llamadas JNI que haría la activity de Java
 * sobre libsonic1native.so. Ver docs/SYMBOLS.md. */
int  sonic1_init(so_module *mod, const char *data_dir, const char *save_file);
void sonic1_frame(void);
void sonic1_shutdown(void);
void sonic1_key(int32_t keycode, int32_t action);
void sonic1_touch(int32_t id, float x, float y, int32_t action);
