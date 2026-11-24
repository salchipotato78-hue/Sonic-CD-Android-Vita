# Símbolos de libsonic1native.so (APK Sonic 1, Android 2013, v1.0.0)

Extraídos con `readelf --dyn-syms` del `lib/armeabi-v7a/libsonic1native.so` del APK.
Solo son nombres de símbolos. El orden de llamadas y las firmas en `source/sonic1/driver.c` son suposiciones que hay que verificar.

## Librerías necesarias (DT_NEEDED)
libGLESv1_CM.so, libOpenSLES.so, liblog.so, libm.so, libstdc++.so, libc.so, libdl.so

## Exports JNI (Java_com_sega_sonic1_*)
```
Java_com_sega_sonic1_Sonic1Activity_AddTouch
Java_com_sega_sonic1_Sonic1Activity_ClearTouchData
Java_com_sega_sonic1_Sonic1Activity_GetEngineGlobalVariable
Java_com_sega_sonic1_Sonic1Activity_RemoveTouch
Java_com_sega_sonic1_Sonic1Activity_SendGameMessage
Java_com_sega_sonic1_Sonic1Activity_SetEngineGlobalVariable
Java_com_sega_sonic1_Sonic1Activity_SetEngineOnlineCapabilities
Java_com_sega_sonic1_Sonic1Activity_SetGameLanguage
Java_com_sega_sonic1_Sonic1Activity_SetGameMode
Java_com_sega_sonic1_Sonic1Activity_SetKeyData
Java_com_sega_sonic1_Sonic1Activity_SetTouch
Java_com_sega_sonic1_Sonic1Activity_SetVersionNumber
Java_com_sega_sonic1_Sonic1Activity_ShutdownRetroEngine
Java_com_sega_sonic1_Sonic1Renderer_CreateAudioBufferQueue
Java_com_sega_sonic1_Sonic1Renderer_CreateDeviceTextures
Java_com_sega_sonic1_Sonic1Renderer_GetAchievementID
Java_com_sega_sonic1_Sonic1Renderer_GetHapticEffectNum
Java_com_sega_sonic1_Sonic1Renderer_ProcessMainLoop
Java_com_sega_sonic1_Sonic1Renderer_ReceiveCallbackID
Java_com_sega_sonic1_Sonic1Renderer_SetEngineGlobalVariable
Java_com_sega_sonic1_Sonic1Renderer_SetScreenDimensions
Java_com_sega_sonic1_Sonic1Renderer_StartAudioBufferQueue
Java_com_sega_sonic1_Sonic1Renderer_StartupRetroEngine
Java_com_sega_sonic1_Sonic1Renderer_StopAudioBufferQueue
Java_com_sega_sonic1_Sonic1View_SetAPKDirectory
Java_com_sega_sonic1_Sonic1View_SetSaveFileName
Java_com_sega_sonic1_Sonic1View_SetScreenDimensions
```

## Imports no GL
```

__cxa_atexit
__cxa_begin_cleanup
__cxa_call_unexpected
__cxa_finalize
__cxa_type_match
__errno
__gnu_Unwind_Find_exidx
__stack_chk_fail
__stack_chk_guard
_toupper_tab_
abort
atan2f
calloc
ceilf
cosf
fclose
floorf
fopen
fread
free
fseek
ftell
fwrite
lrand48
malloc
memchr
memcmp
memcpy
memset
pthread_mutex_destroy
pthread_mutex_init
pthread_mutex_lock
pthread_mutex_unlock
qsort
raise
realloc
sinf
slCreateEngine
strcpy
strlen
tanf
```

## Notas
- Nombres de datos referenciados: `Data/Game/GameConfig.bin`, `Data/Sprites/`, `Data/Stages/`, `Data/Music/`, `Data/SoundFX/`, `Data/Palettes/`, `Data/Animations/`, `Bytecode/GlobalCode.bin`.
- Archivo de datos en el APK: `assets/Data.rsdk.xmf`. Verificar si el motor espera `data.rsdk`.
- GL: vitaGL tiene `ffp.c` (pipeline fijo), así que las llamadas `gl*Pointer`/`glLoadMatrixf` tienen soporte parcial.

## Firmas verificadas (desensamblado ARMv7)

| Función | Firma inferida | Notas |
|---|---|---|
| `Sonic1View_SetAPKDirectory` | `(env, obj, jstring path, int32, uint8 flag)` | Llama a `GetStringUTFChars` y luego `AddToRSDKContainer(path, flag)`. `AddToRSDKContainer` abre el archivo y comprueba la cabecera `RSDK`. Por eso el `path` debe ser el archivo de datos, no un directorio. |
| `Sonic1View_SetSaveFileName` | `(env, obj, jstring)` | Mismo patrón de `GetStringUTFChars`. |
| `Sonic1View_SetScreenDimensions` | `(env, obj, int32 w, int32 h)` | Convierte a float internamente. |
| `Sonic1Renderer_StartupRetroEngine` | `(env, obj)` | Inicializa tablas, config, dispositivo de render, audio y objetos nativos. Solo se ejecuta una vez. |
| `Sonic1Renderer_CreateDeviceTextures` | `(env, obj)` | Vacía. |
| `Sonic1Renderer_CreateAudioBufferQueue` / `StartAudioBufferQueue` | `(env, obj)` | |
| `Sonic1Renderer_ProcessMainLoop` | `(env, obj)` | Un frame. |
| `Sonic1Activity_SetKeyData` | `(env, obj, int32 index 0..7, int32 state)` | `index` es un índice de botón, no un keycode de Android. El segundo argumento no está verificado. |
| `Sonic1Activity_SetTouch` / `AddTouch` | `(env, obj, float x, float y, int32 id)` | `AddTouch` sigue el mismo patrón según el desensamblado parcial. |
| `Sonic1Activity_RemoveTouch` | `(env, obj, int32 id)` | |
| `Sonic1Activity_ClearTouchData` | `(env, obj)` | |
| `Sonic1Activity_ShutdownRetroEngine` | `(env, obj)` | Libera render y audio. |

Datos del APK: `assets/Data.rsdk.xmf` empieza con la cabecera `RSDKvBD`, así que el motor lo acepta con ese nombre. No hace falta renombrarlo a `data.rsdk`.

## Secuencia de configuración (decodificada del bytecode de Sonic1Activity)

- `onCreate`: `SetGameLanguage(idx)` con idx según el string de recursos: en=0, fr=1, it=2, de=3, es=4, ja=5, pt=6, ru=7, ko=8, zh-Hant=9, zh-Hans=10 (por defecto 0). Luego `SetVersionNumber(versionName)` y `SetEngineOnlineCapabilities(0)`.
- `onWindowFocusChanged(true)`: `Sonic1View.onResume`, `setRenderMode(1)`, `SetGameMode(5)`. Con `false`: `setRenderMode(0)` y `onPause`.
- `CheckGameResources` (no usado en el port): comprueba `/Android/data/com.sega.sonic1/files/Version.txt`; si no coincide llama `SetGameMode(7)` (ruta de copia de recursos, no implementada).
- `ReadPowerAController`: `SetEngineGlobalVariable(8, ...)` solo con mando PowerA detectado (no implementado).
- Audio: el `.so` importa de libOpenSLES `slCreateEngine`, `SL_IID_ENGINE`, `SL_IID_ANDROIDSIMPLEBUFFERQUEUE`, `SL_IID_BUFFERQUEUE`, `SL_IID_PLAY`, `SL_IID_VOLUME`. Implementado en `source/reimpl/sles.c` sobre `sceAudioOut` (sin verificar en hardware).
