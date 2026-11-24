# Sonic 1 (Android 2013) – PS Vita port

Port del APK oficial de Sonic the Hedgehog (2013, Android) a PS Vita mediante un loader de `.so`
(basado en [solobop / soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate)).
El binario `.so` y los datos del juego **no** se incluyen en este repositorio.

## Qué necesitas

- Tu propia copia legal del APK de Sonic 1 (Android, 2013).
- Un PS Vita con homebrew (taiHEN, VitaShell) y `kubridge.skprx` instalado.
- VitaSDK-softfp para compilar (o usar el workflow de GitHub Actions).

## Archivos del juego (no se suben al repo)

1. Abre el APK con un explorador ZIP y extrae `lib/armeabi-v7a/<libmain>.so`.
2. Copia los datos del juego (`data.rsdk` u otros assets) según la estructura que indique el port.
3. En el Vita, colócalos en `ux0:data/sonic1/`.

## Compilar

```sh
git clone --recurse-submodules <url-de-este-repo>
cd sonic1-vita
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

El VPK queda en `build/`. Para el `.so` con otro nombre o ruta:

```sh
cmake -S . -B build -DSO_PATH="ux0:data/sonic1/<nombre>.so"
```

## Estado

- [ ] Confirmar nombre del `.so` y símbolos importados del APK de 2013.
- [ ] Completar `source/dynlib.c` con los símbolos que el `.so` requiera.
- [ ] Probar arranque, gráficos (vitaGL), audio y controles en hardware.
- [ ] Completar el paso de instalación de VitaSDK-softfp en CI.

## Créditos

- Loader: solobop / soloader-boilerplate y so_util, de Volodymyr Atamanenko, basado en el trabajo de TheFloW.
- FalsoJNI y vitaGL, de Rinnegatamante y colaboradores.
- Sonic the Hedgehog © SEGA. Este proyecto no está afiliado a SEGA.

## Licencia

Ver `LICENSE` (heredada del template). El código de este port no incluye assets de SEGA.
