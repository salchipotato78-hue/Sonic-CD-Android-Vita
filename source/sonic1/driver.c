/*
 * Driver JNI para libRetroEngineJNI.so (Sonic 1, Android v3.0.0, libRetroEngineJNI.so).
 *
 * Firmas verificadas desensamblando las funciones Java_com_sega_sonic1_* (ARMv7):
 *   Sonic1View_SetAPKDirectory(env, obj, jstring path, int32 (no usado), uint8 flag)
 *       -> GetStringUTFChars(path) y AddToRSDKContainer(path, flag). El contenedor
 *          abre el archivo, lee la cabecera "RSDK" y lo registra. Hay que pasar
 *          el archivo de datos completo, no un directorio.
 *   Sonic1View_SetSaveFileName(env, obj, jstring)
 *   Sonic1View_SetScreenDimensions(env, obj, int32 w, int32 h)
 *   Sonic1Renderer_StartupRetroEngine(env, obj)   (inicializa motor, render, audio, script)
 *   Sonic1Renderer_CreateDeviceTextures(env, obj) (vacía; no hace nada)
 *   Sonic1Renderer_CreateAudioBufferQueue(env, obj)
 *   Sonic1Renderer_StartAudioBufferQueue(env, obj)
 *   Sonic1Renderer_ProcessMainLoop(env, obj)      (un frame)
 *   Sonic1Activity_SetKeyData(env, obj, int32 indice 0..7, int32 estado)  -- índice, no keycode de Android
 *   Sonic1Activity_SetTouch(env, obj, float x, float y, int32 id)
 *   Sonic1Activity_AddTouch(env, obj, float x, float y, int32 id)          -- supuesto, mismo patrón
 *   Sonic1Activity_RemoveTouch(env, obj, int32 id)
 *   Sonic1Activity_ClearTouchData(env, obj)
 *   Sonic1Activity_ShutdownRetroEngine(env, obj)
 *
 * Pendiente de verificar: segundo argumento de SetKeyData, el significado del
 * flag de SetAPKDirectory (se pasa 0), y el mapeo de botones Android -> índices.
 */
#include "driver.h"

#include <stdio.h>
#include <falso_jni/FalsoJNI.h>
#include "utils/logger.h"

typedef JNIEnv *JNIEnvPtr;   /* JNIEnv de FalsoJNI: puntero a puntero a la tabla */
typedef void *JObj;
typedef void *JStr;

typedef void (*fn_str)(JNIEnvPtr, JObj, JStr);
typedef void (*fn_apk)(JNIEnvPtr, JObj, JStr, int32_t, uint8_t);
typedef void (*fn_void)(JNIEnvPtr, JObj);
typedef void (*fn_int2)(JNIEnvPtr, JObj, int32_t, int32_t);
typedef void (*fn_key)(JNIEnvPtr, JObj, int32_t, int32_t);
typedef void (*fn_touch)(JNIEnvPtr, JObj, float, float, int32_t);
typedef void (*fn_id)(JNIEnvPtr, JObj, int32_t);
typedef void (*fn_int1)(JNIEnvPtr, JObj, int32_t);

static so_module *g_mod;
static JNIEnvPtr  g_env;
/* Objetos fake: el .so no los usa como campos en las funciones que llamamos,
 * pero deben ser no nulos. Si un crash apunta a un acceso sobre ellos, revisar. */
static char       fake_view_obj[64];
static char       fake_renderer_obj[64];
static char       fake_activity_obj[64];
static JObj       g_view     = fake_view_obj;
static JObj       g_renderer = fake_renderer_obj;
static JObj       g_activity = fake_activity_obj;

#define SYM(fn) ((void *)so_symbol(g_mod, "Java_com_christianwhitehead_rsdk_RetroEngine_" fn))

/* jstring real: el .so llama a GetStringUTFChars sobre él, así que debe salir
 * de la tabla de FalsoJNI (NewStringUTF), no ser un char* directo. */
static JStr make_jstring(const char *s) {
    return (JStr)(*g_env)->NewStringUTF(g_env, s);
}

static fn_apk  p_set_apk;
static fn_str  p_set_save;
static fn_int2 p_set_dims;
static fn_void p_bind_env, p_startup, p_create_audio, p_start_audio, p_frame, p_shutdown, p_touch_clear;
static fn_key  p_key;
static fn_touch p_touch_set, p_touch_add;
static fn_id   p_touch_remove;
/* Configuración que el Java hace en Sonic1Activity.onCreate / onWindowFocusChanged. */
static fn_int1 p_set_language, p_set_online, p_set_mode;
static fn_str  p_set_version;

static int resolve_all(void) {
    p_set_language = SYM("setGameLanguage");
    p_set_online  = SYM("setOnlineCapabilities");
    p_set_mode    = SYM("setGameMode");
    p_set_version = SYM("setVersionNumber");
    p_set_apk     = SYM("setAPKDirectory");
    p_set_save    = SYM("setSaveFileName");
    p_set_dims    = SYM("setScreenDimensions");
    p_bind_env    = SYM("bindJavaENV");
    p_startup     = SYM("startup");
    p_create_audio = SYM("createAudioBufferQueue");
    p_start_audio = SYM("startAudioBufferQueue");
    p_frame       = SYM("processMainLoop");
    p_shutdown    = SYM("shutdown");
    p_key         = SYM("setKeyData");
    p_touch_set   = SYM("setTouch");
    p_touch_add   = SYM("addTouch");
    p_touch_remove = SYM("removeTouch");
    p_touch_clear = SYM("clearTouchData");

    if (!p_set_apk || !p_set_save || !p_set_dims || !p_startup || !p_frame) {
        l_fatal("Faltan símbolos JNI obligatorios de libRetroEngineJNI.so (ver docs/SYMBOLS.md).");
        return -1;
    }
    return 0;
}

int sonic1_init(so_module *mod, const char *data_file, const char *save_file) {
    g_mod = mod;
    if (resolve_all() < 0)
        return -1;

    /* jni_init() ya corrió en soloader_init_all(), así que &jni es válido aquí.
     * Sin esto el .so hace ldr [NULL] en Java_..._SetAPKDirectory (crash 0x98010864). */
    g_env = &jni;

    /* v3.0.0 guarda el JNIEnv en un global interno (javaEnv) con bindJavaENV.
     * Sin esta llamada el .so hace (*javaEnv)->GetObjectClass con javaEnv == NULL:
     * crash en ldr r2,[r0] con r0 = 0 (core: PC 0x98012cd4). */
    if (p_bind_env) p_bind_env(g_env, g_activity);

    /* Mismo orden que Sonic1Activity.onCreate:
     *   SetGameLanguage(0) -> 0 = inglés. El Java elige el índice por el string de
     *   recursos ("en" -> 0, "fr" -> 1, ..., "es" -> 4). Pendiente: leer el idioma del Vita.
     *   SetVersionNumber("1.0.0") -> versionName del APK.
     *   SetEngineOnlineCapabilities(0). */
    if (p_set_language) p_set_language(g_env, g_activity, 0);
    if (p_set_version)  p_set_version(g_env, g_activity, make_jstring("3.0.0"));
    if (p_set_online)   p_set_online(g_env, g_activity, 0);

    /* data_file es el archivo RSDK completo (p. ej. DATA_PATH "Data.rsdk.xmf"). */
    p_set_apk(g_env, g_view, make_jstring(data_file), 0, 0);
    p_set_save(g_env, g_view, make_jstring(save_file));
    /* Resolución real del surface de la Vita. El Java original pasa el tamaño del view. */
    p_set_dims(g_env, g_view, 960, 544);

    p_startup(g_env, g_renderer);
    if (p_create_audio) p_create_audio(g_env, g_renderer);
    if (p_start_audio)  p_start_audio(g_env, g_renderer);

    /* Sonic1Activity.onWindowFocusChanged(true) llama SetGameMode(5) tras el surface.
     * Sin esto el motor no entra al modo de juego (y no muestra los controles táctiles). */
    if (p_set_mode) p_set_mode(g_env, g_activity, 5);

    l_success("Sonic 1: motor inicializado.");
    return 0;
}

void sonic1_frame(void) {
    if (p_frame)
        p_frame(g_env, g_renderer);
}

void sonic1_shutdown(void) {
    if (p_shutdown)
        p_shutdown(g_env, g_activity);
}

/* Índices de SetKeyData, verificados con el bytecode de Sonic1Activity.onKeyDown
 * (sparse-switch sobre keycode -> SetKeyData(índice, estado)):
 *   0 up, 1 down, 2 left, 3 right, 4 A (salto), 5 B, 6 C, 7 start.
 * Estado: 1 = pulsado, 0 = soltado (onKeyUp). */
static int32_t key_to_index(int32_t keycode) {
    switch (keycode) {
        case 19:  return 0; /* AKEYCODE_DPAD_UP */
        case 20:  return 1; /* AKEYCODE_DPAD_DOWN */
        case 21:  return 2; /* AKEYCODE_DPAD_LEFT */
        case 22:  return 3; /* AKEYCODE_DPAD_RIGHT */
        case 96:  return 4; /* AKEYCODE_BUTTON_A (Cross, salto) */
        case 97:  return 5; /* AKEYCODE_BUTTON_B (Circle) */
        case 99:  return 6; /* AKEYCODE_BUTTON_X (Square) */
        case 100: return 6; /* AKEYCODE_BUTTON_Y (Triangle), también C en el original */
        case 108: return 7; /* AKEYCODE_BUTTON_START */
        default:  return -1;
    }
}

void sonic1_key(int32_t keycode, int32_t action) {
    int32_t idx = key_to_index(keycode);
    if (idx < 0 || !p_key)
        return;
    /* action: CONTROLS_ACTION_UP = 0, CONTROLS_ACTION_DOWN = 1 -> estado 0/1 */
    p_key(g_env, g_activity, idx, action == 1 ? 1 : 0);
}

void sonic1_touch(int32_t id, float x, float y, int32_t action) {
    /* Igual que Sonic1Activity.onTouchEvent (switch sobre MotionEvent.getAction):
     * ACTION_DOWN/POINTER_DOWN -> AddTouch(x, y, id)
     * ACTION_UP/POINTER_UP     -> RemoveTouch(id)
     * ACTION_MOVE              -> SetTouch(x, y, id) por cada puntero
     * ControlsAction: UP = 0, DOWN = 1, MOVE = 2. Las coordenadas van en
     * píxeles del surface (960x544), como las pasa el Java original. */
    if (action == 1 && p_touch_add)
        p_touch_add(g_env, g_activity, x, y, id);
    else if (action == 0 && p_touch_remove)
        p_touch_remove(g_env, g_activity, id);
    else if (action == 2 && p_touch_set)
        p_touch_set(g_env, g_activity, x, y, id);
}
