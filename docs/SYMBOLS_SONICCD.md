# Símbolos de libRetroEngineJNI.so (Sonic CD Classic 1.0.9, build 46, armeabi-v7a)

Extraídos con `readelf --dyn-syms -W` del APK. Solo nombres; las firmas y el orden de llamada se verifican desensamblando.

## Librerías necesarias (DT_NEEDED)
libGLESv1_CM.so, libOpenSLES.so, libm.so, liblog.so, libandroid.so, libdl.so, libc.so

## Exports JNI (`Java_com_christianwhitehead_rsdk_RetroEngine_*` y `Java_com_sega_f2fextension_F2FAndroidJNI_*`)
Las del grupo `com_sega_f2fextension` (ads, IAP, GDPR, banners) no se necesitan en el port.

```
Java_com_christianwhitehead_rsdk_RetroEngine_ActiveGame
Java_com_christianwhitehead_rsdk_RetroEngine_addTouch
Java_com_christianwhitehead_rsdk_RetroEngine_bindJavaENV
Java_com_christianwhitehead_rsdk_RetroEngine_bindJavaObject2JNI
Java_com_christianwhitehead_rsdk_RetroEngine_clearTouchData
Java_com_christianwhitehead_rsdk_RetroEngine_createAudioBufferQueue
Java_com_christianwhitehead_rsdk_RetroEngine_createDeviceTextures
Java_com_christianwhitehead_rsdk_RetroEngine_getGlobalVariable
Java_com_christianwhitehead_rsdk_RetroEngine_getOnlineCapabilities
Java_com_christianwhitehead_rsdk_RetroEngine_processMainLoop
Java_com_christianwhitehead_rsdk_RetroEngine_receive2PVSMatchCode
Java_com_christianwhitehead_rsdk_RetroEngine_removeTouch
Java_com_christianwhitehead_rsdk_RetroEngine_setAPKDirectory
Java_com_christianwhitehead_rsdk_RetroEngine_setBitDepth
Java_com_christianwhitehead_rsdk_RetroEngine_setGameDeviceType
Java_com_christianwhitehead_rsdk_RetroEngine_setGameLanguage
Java_com_christianwhitehead_rsdk_RetroEngine_setGameMessage
Java_com_christianwhitehead_rsdk_RetroEngine_setGameMode
Java_com_christianwhitehead_rsdk_RetroEngine_setGlobalVariable
Java_com_christianwhitehead_rsdk_RetroEngine_setKeyData
Java_com_christianwhitehead_rsdk_RetroEngine_setOnlineCapabilities
Java_com_christianwhitehead_rsdk_RetroEngine_setRSDKPath
Java_com_christianwhitehead_rsdk_RetroEngine_setRefreshRateRatio
Java_com_christianwhitehead_rsdk_RetroEngine_setSaveFileName
Java_com_christianwhitehead_rsdk_RetroEngine_setScreenDimensions
Java_com_christianwhitehead_rsdk_RetroEngine_setTouch
Java_com_christianwhitehead_rsdk_RetroEngine_setVersionNumber
Java_com_christianwhitehead_rsdk_RetroEngine_shutdown
Java_com_christianwhitehead_rsdk_RetroEngine_startAudioBufferQueue
Java_com_christianwhitehead_rsdk_RetroEngine_startup
Java_com_christianwhitehead_rsdk_RetroEngine_stopAudioBufferQueue
```

## Imports no mencionados en `source/dynlib.c` del repo de Sonic 1 (pendientes)
```
dladdr
funopen
getnameinfo
getpwuid
getrlimit
getuid
initgroups
kill
mlock
mprotect
pthread_rwlock_destroy
pthread_rwlock_init
pthread_rwlock_rdlock
pthread_rwlock_unlock
pthread_rwlock_wrlock
setgid
setuid
syslog
```

## Imports de OpenSL ES y GLES1
Los `gl*` y `SL_*` no aparecen por nombre en `dynlib.c`; se resuelven contra vitaGL y las reimplementaciones de `reimpl/sles.c`. Verificar al clonar los submódulos.

## Imports GLES1 usados (`gl*`)
```
glBindFramebufferOES glBindTexture glBlendFunc glClear glClearColor glColorPointer glDeleteTextures glDisable glDisableClientState glDrawElements glEnable glEnableClientState glFramebufferTexture2DOES glGenFramebuffersOES glGenTextures glGetIntegerv glLightfv glLoadIdentity glLoadMatrixf glMatrixMode glMultMatrixf glNormalPointer glOrthof glPopMatrix glPushMatrix glRotatef glScalef glTexCoordPointer glTexImage2D glTexParameterf glTexParameteri glTexSubImage2D glTranslatef glVertexPointer glViewport 
```

## Firmas observadas en el desensamblado (ARMv7, `llvm-objdump`)

| Función | Observado | Notas |
|---|---|---|
| `startup` | guarda `JNIEnv*` en un global y arranca el motor una sola vez | |
| `setScreenDimensions` | enteros `w` (r2) y `h` (r3) | |
| `addTouch` | `x`,`y` float (se convierten con `vcvt.s32.f32`), `id` en pila | |
| `setAPKDirectory` | `jstring` en r2, llama a `GetStringUTFChars` | flag en pila |
| `setGameMessage` / `setGameLanguage` | byte (`uxtb`) | |
| `setGameMode` | guarda byte en un global | valor 5 viene del driver de Sonic 1, sin verificar |
| `setKeyData` | índice `(r2 & 0xff) - 1` con tabla de salto, rango 1..8 | el driver de Sonic 1 usa 0..7; **verificar** |
| `createDeviceTextures` | vacía (`bx lr`) | |

Pendiente: verificar en el `classes*.dex` de Sonic CD el valor de `SetGameMode` y los índices de `SetKeyData`.
