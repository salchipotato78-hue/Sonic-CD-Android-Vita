/*
 * Minimal OpenSL ES sobre sceAudioOut, para libRetroEngineJNI.so (v3.0.0).
 *
 * El .so importa de libOpenSLES.so: slCreateEngine, SL_IID_ENGINE,
 * SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_BUFFERQUEUE, SL_IID_PLAY y
 * SL_IID_VOLUME. Este archivo implementa solo esa ruta:
 *
 *   slCreateEngine -> Engine.GetInterface(ENGINE) -> CreateAudioPlayer(src = buffer queue PCM)
 *   Player.Realize -> abre el puerto sceAudioOut (la muestra se mezcla en estéreo 16 bits)
 *   Player.GetInterface(PLAY | BUFFERQUEUE | ANDROIDSIMPLEBUFFERQUEUE | VOLUME)
 *   BufferQueue.RegisterCallback / Enqueue; PLAY.SetPlayState(PLAYING) arranca un hilo
 *     que consume los buffers encolados y llama al callback al terminar cada uno,
 *     igual que el hilo de audio de Android.
 *
 * Los buffers se usan sin copiar: el motor los mantiene vivos hasta su callback.
 * Sin mezcla de salida ni resampleo: si el motor pide una frecuencia que el Vita
 * no soporta, se usa 48000 Hz (la velocidad puede ser incorrecta; se registra).
 *
 * Pendiente de verificar en hardware: la frecuencia que pide el motor (SLDataFormat_PCM)
 * y que el callback se dispare como se espera.
 */
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#ifndef SL_API
#define SL_API
#endif

#include <pthread.h>
#include <stdio.h>
#include <psp2/io/fcntl.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include "utils/logger.h"

#define SLES_GRAIN_FRAMES 1024   /* frames por sceAudioOutOutput */
#define SLES_QUEUE_MAX    8
/* Frecuencia real de los datos PCM del juego. El motor declara 48000 Hz, pero el audio
 * suena agudo y rápido, así que los datos reales están a otra frecuencia.
 * Pon aquí la frecuencia correcta en Hz (p. ej. 44100, 32000 o 22050).
 * 0 = usar la frecuencia que declara el motor. */
#define SLES_FORCE_SRC_RATE 0u
/* Canales de los datos PCM. 0 = usar los que declara el motor; 1 = mono; 2 = estéreo. */
#define SLES_FORCE_CHANNELS 0u

/* ---- IIDs: direcciones únicas, comparadas por puntero ---- */
static const SLuint32 iid_tag_engine = 1, iid_tag_abq = 2, iid_tag_bq = 3, iid_tag_play = 4,
                      iid_tag_volume = 5, iid_tag_metaext = 6, iid_tag_pitch = 7,
                      iid_tag_prefetch = 8, iid_tag_seek = 9;

SL_API const SLInterfaceID SL_IID_ENGINE = (SLInterfaceID)&iid_tag_engine;
SL_API const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE = (SLInterfaceID)&iid_tag_abq;
SL_API const SLInterfaceID SL_IID_BUFFERQUEUE = (SLInterfaceID)&iid_tag_bq;
SL_API const SLInterfaceID SL_IID_PLAY = (SLInterfaceID)&iid_tag_play;
SL_API const SLInterfaceID SL_IID_VOLUME = (SLInterfaceID)&iid_tag_volume;
SL_API const SLInterfaceID SL_IID_METADATAEXTRACTION = (SLInterfaceID)&iid_tag_metaext;
SL_API const SLInterfaceID SL_IID_PITCH = (SLInterfaceID)&iid_tag_pitch;
SL_API const SLInterfaceID SL_IID_PREFETCHSTATUS = (SLInterfaceID)&iid_tag_prefetch;
SL_API const SLInterfaceID SL_IID_SEEK = (SLInterfaceID)&iid_tag_seek;

/* ---- Estado del reproductor (un único player) ---- */
typedef struct SlesSink {
    int port;                  /* puerto sceAudioOut, -1 si no abierto */
    SLuint32 channels;
    SLuint32 rate;
    SLuint32 bits;
    SLuint32 playState;        /* SL_PLAYSTATE_* */
    volatile int run;          /* hilo vivo */
    pthread_t thread;
    int thread_started;
    pthread_mutex_t mtx;

    /* cola de buffers encolados por el motor */
    const void *q_ptr[SLES_QUEUE_MAX];
    SLuint32    q_size[SLES_QUEUE_MAX];
    int         q_head, q_count;
    /* buffer que se está consumiendo */
    const uint8_t *cur_ptr;
    SLuint32       cur_size, cur_off;

    SLuint32 volume;           /* milibeles, solo guardado */
    int      callback_set;
    void    *cb;               /* SLAndroidSimpleBufferQueueCallback o SLBufferQueueCallback */
    void    *cb_ctx;
    void    *cb_self;          /* interfaz BufferQueue que se pasa al callback */
} SlesSink;

static SlesSink g_sink = { .port = -1, .playState = SL_PLAYSTATE_STOPPED };
static unsigned g_decl_rate, g_decl_ch, g_decl_bits;   /* lo que declara el motor */

/* ---- Vtables ---- */
struct SlesObj { const struct SLObjectItf_ *vt; };
struct SlesEngineItf { const struct SLEngineItf_ *vt; };
struct SlesPlayItf { const struct SLPlayItf_ *vt; };
struct SlesBqItf { const struct SLAndroidSimpleBufferQueueItf_ *vt; };
struct SlesVolItf { const struct SLVolumeItf_ *vt; };

static struct SlesObj g_engine_obj, g_player_obj;
static struct SlesEngineItf g_engine_itf;
static struct SlesPlayItf g_play_itf;
static struct SlesBqItf g_bq_itf;
static struct SlesVolItf g_vol_itf;

/* ---- RESAMPLER BEGIN ---- */
/* El motor puede pedir cualquier frecuencia (p. ej. 22050 Hz); el puerto de Vita se abre
 * siempre a 48000 Hz y aquí se convierte por interpolación lineal. Sin esto, la muestra
 * se reproduce a 48000 Hz y suena acelerada y aguda.
 * rs_buf guarda frames estéreo 16 bits de entrada; rs_pos es la posición fraccionaria
 * (Q16) del siguiente frame de salida dentro de rs_buf. */
#define SLES_OUT_RATE      48000u
#define SLES_RS_BUF_FRAMES (SLES_GRAIN_FRAMES * 2 + 8)

static int16_t rs_buf[SLES_RS_BUF_FRAMES * 2];
static uint32_t rs_have;   /* frames válidos en rs_buf */
static uint32_t rs_pos;    /* Q16, en [0, 65536) */
static uint32_t rs_step;   /* Q16: frames de entrada por frame de salida */

static void rs_init(uint32_t in_rate) {
    if (in_rate == 0 || in_rate > 96000) {
        l_warn("sles: frecuencia %u fuera de rango, uso 48000 Hz", (unsigned)in_rate);
        in_rate = SLES_OUT_RATE;
    }
    rs_step = (uint32_t)(((uint64_t)in_rate << 16) / SLES_OUT_RATE);
    l_info("sles: el motor pide %u Hz, remuestreo a %u Hz", (unsigned)in_rate, SLES_OUT_RATE);

    /* Diagnóstico: escribe la frecuencia pedida en un archivo legible desde VitaShell. */
    {
        char dbg[128];
        int n = snprintf(dbg, sizeof(dbg),
                         "declarado=%u Hz %u canales %u bits\n"
                         "usado=%u Hz canales=%u step=%u salida=%u Hz\n",
                         g_decl_rate, g_decl_ch, g_decl_bits,
                         (unsigned)in_rate, (unsigned)g_sink.channels,
                         (unsigned)rs_step, (unsigned)SLES_OUT_RATE);
        SceUID fd = sceIoOpen(DATA_PATH "audio_debug.txt",
                              SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd >= 0) {
            sceIoWrite(fd, dbg, (SceSize)n);
            sceIoClose(fd);
        }
    }
    rs_pos = 0;
    rs_have = 0;
}

/* Frames de entrada necesarios para producir un grano de salida. */
static uint32_t rs_frames_needed(void) {
    uint64_t last = ((uint64_t)rs_pos + (uint64_t)(SLES_GRAIN_FRAMES - 1) * rs_step) >> 16;
    return (uint32_t)last + 2;
}

/* Produce SLES_GRAIN_FRAMES frames estéreo en out y descarta los frames de entrada ya usados.
 * Requiere rs_have >= rs_frames_needed(). */
static void rs_output(int16_t *out) {
    for (uint32_t k = 0; k < SLES_GRAIN_FRAMES; k++) {
        uint64_t t = (uint64_t)rs_pos + (uint64_t)k * rs_step;
        uint32_t i0 = (uint32_t)(t >> 16);
        int64_t  f  = (int64_t)(t & 0xFFFF);
        for (int c = 0; c < 2; c++) {
            int32_t a = rs_buf[i0 * 2 + c];
            int32_t b = rs_buf[(i0 + 1) * 2 + c];
            out[k * 2 + c] = (int16_t)(a + (int32_t)(((int64_t)(b - a) * f) >> 16));
        }
    }
    uint64_t total = (uint64_t)rs_pos + (uint64_t)SLES_GRAIN_FRAMES * rs_step;
    uint32_t consume = (uint32_t)(total >> 16);
    memmove(rs_buf, rs_buf + consume * 2, (rs_have - consume) * 2 * sizeof(int16_t));
    rs_have -= consume;
    rs_pos = (uint32_t)(total & 0xFFFF);
}
/* ---- RESAMPLER END ---- */

/* ---- Hilo de audio ---- */
static void fire_callback(void) {
    if (g_sink.callback_set && g_sink.cb) {
        /* Ambos tipos de callback tienen la forma (interfaz, ctx). */
        ((void (*)(void *, void *))g_sink.cb)(g_sink.cb_self, g_sink.cb_ctx);
    }
}

/* Saca el siguiente buffer de la cola. Devuelve 1 si hay uno nuevo en cur_*. */
static int pop_buffer(void) {
    int got = 0;
    pthread_mutex_lock(&g_sink.mtx);
    if (g_sink.q_count > 0) {
        g_sink.cur_ptr = (const uint8_t *)g_sink.q_ptr[g_sink.q_head];
        g_sink.cur_size = g_sink.q_size[g_sink.q_head];
        g_sink.cur_off = 0;
        g_sink.q_head = (g_sink.q_head + 1) % SLES_QUEUE_MAX;
        g_sink.q_count--;
        got = 1;
    }
    pthread_mutex_unlock(&g_sink.mtx);
    return got;
}

/* Saca `need` bytes PCM del motor; rellena con silencio si la cola está vacía. */
static void pull_bytes(uint8_t *dst, SLuint32 need) {
    SLuint32 got = 0;
    while (got < need && g_sink.run) {
        if (g_sink.cur_off >= g_sink.cur_size) {
            if (!pop_buffer()) {
                /* Cola vacía: pedir más al motor y reintentar una vez. */
                fire_callback();
                if (!pop_buffer())
                    break;   /* sin datos: rellenar con silencio */
            }
        }
        SLuint32 avail = g_sink.cur_size - g_sink.cur_off;
        SLuint32 take = need - got < avail ? need - got : avail;
        memcpy(dst + got, g_sink.cur_ptr + g_sink.cur_off, take);
        got += take;
        g_sink.cur_off += take;
        if (g_sink.cur_off >= g_sink.cur_size) {
            /* Buffer terminado: avisar al motor para que encole el siguiente. */
            fire_callback();
        }
    }
    if (got < need)
        memset(dst + got, 0, need - got);
}

/* Saca n_frames del motor y los deja como estéreo 16 bits en dst. */
static void pull_frames(int16_t *dst, uint32_t n_frames, SLuint32 channels) {
    static uint8_t raw[SLES_RS_BUF_FRAMES * 2 * 2];
    pull_bytes(raw, n_frames * channels * 2);
    const int16_t *src = (const int16_t *)raw;
    for (uint32_t i = 0; i < n_frames; i++) {
        if (channels == 1) {
            dst[2 * i]     = src[i];
            dst[2 * i + 1] = src[i];
        } else {
            dst[2 * i]     = src[2 * i];
            dst[2 * i + 1] = src[2 * i + 1];
        }
    }
}

static void *audio_thread(void *arg) {
    (void)arg;
    const SLuint32 channels = g_sink.channels ? g_sink.channels : 2;
    static int16_t out[SLES_GRAIN_FRAMES * 2];

    while (g_sink.run) {
        if (g_sink.playState != SL_PLAYSTATE_PLAYING) {
            sceKernelDelayThread(5000);
            continue;
        }

        uint32_t need = rs_frames_needed();
        if (rs_have < need) {
            pull_frames(rs_buf + rs_have * 2, need - rs_have, channels);
            rs_have = need;
        }
        rs_output(out);

        if (g_sink.port >= 0)
            sceAudioOutOutput(g_sink.port, out);
    }
    return NULL;
}

/* ---- Objeto Player / Engine ---- */
static SLresult obj_realize(SLObjectItf self, SLboolean async) {
    (void)async;
    /* El motor también se realiza: solo el player debe abrir el sink, cuando ya conoce el formato. */
    if (self != (SLObjectItf)&g_player_obj)
        return SL_RESULT_SUCCESS;
    if (g_sink.port < 0) {
        rs_init(g_sink.rate);
        g_sink.port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, SLES_GRAIN_FRAMES,
                                          SLES_OUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
        if (g_sink.port < 0) {
            l_error("sles: sceAudioOutOpenPort fallo: 0x%08x", g_sink.port);
            g_sink.port = -1;
            return SL_RESULT_RESOURCE_ERROR;
        }
    }
    return SL_RESULT_SUCCESS;
}
static SLresult obj_resume(SLObjectItf self, SLboolean async) { (void)self; (void)async; return SL_RESULT_SUCCESS; }
static SLresult obj_get_state(SLObjectItf self, SLuint32 *pState) {
    (void)self;
    if (pState) *pState = (g_sink.port >= 0) ? SL_OBJECT_STATE_REALIZED : SL_OBJECT_STATE_UNREALIZED;
    return SL_RESULT_SUCCESS;
}
static SLresult obj_get_interface(SLObjectItf self, SLInterfaceID iid, void *pInterface) {
    (void)self;
    void *itf = NULL;
    if (iid == SL_IID_PLAY)                       itf = &g_play_itf;
    else if (iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE || iid == SL_IID_BUFFERQUEUE) itf = &g_bq_itf;
    else if (iid == SL_IID_VOLUME)                itf = &g_vol_itf;
    else if (iid == SL_IID_ENGINE)                itf = &g_engine_itf;
    else return SL_RESULT_FEATURE_UNSUPPORTED;
    if (pInterface) *(void **)pInterface = itf;
    return SL_RESULT_SUCCESS;
}
static SLresult obj_destroy(SLObjectItf self) {
    (void)self;
    g_sink.run = 0;
    if (g_sink.thread_started) {
        pthread_join(g_sink.thread, NULL);
        g_sink.thread_started = 0;
    }
    if (g_sink.port >= 0) {
        sceAudioOutReleasePort(g_sink.port);
        g_sink.port = -1;
    }
    return SL_RESULT_SUCCESS;
}

static const struct SLObjectItf_ object_vt = {
    .Realize = obj_realize,
    .Resume = obj_resume,
    .GetState = obj_get_state,
    .GetInterface = obj_get_interface,
    .Destroy = obj_destroy,
};

/* Engine.CreateAudioPlayer: solo soporta fuente buffer queue PCM. El destino
 * (output mix) se ignora: el sink es siempre sceAudioOut. */
static SLresult engine_create_audio_player(SLEngineItf self, SLObjectItf *pPlayer,
                                           SLDataSource *pAudioSrc, SLDataSink *pAudioSnk,
                                           SLuint32 numInterfaces, const SLInterfaceID *pIids,
                                           const SLboolean *pRequired) {
    (void)self; (void)pAudioSnk; (void)numInterfaces; (void)pIids; (void)pRequired;
    if (!pAudioSrc || !pAudioSrc->pFormat || !pPlayer)
        return SL_RESULT_PARAMETER_INVALID;

    const SLDataFormat_PCM *fmt = (const SLDataFormat_PCM *)pAudioSrc->pFormat;
    if (fmt->formatType != SL_DATAFORMAT_PCM)
        return SL_RESULT_CONTENT_UNSUPPORTED;

    g_decl_ch = fmt->numChannels;
    g_decl_rate = fmt->samplesPerSec / 1000;   /* SLDataFormat_PCM usa milihercios */
    g_decl_bits = fmt->bitsPerSample;
    g_sink.channels = fmt->numChannels ? fmt->numChannels : 2;
    if (SLES_FORCE_CHANNELS)
        g_sink.channels = SLES_FORCE_CHANNELS;
    g_sink.rate = g_decl_rate;
    if (SLES_FORCE_SRC_RATE) {
        l_info("sles: frecuencia forzada %u Hz (el motor pidió %u Hz)",
               (unsigned)SLES_FORCE_SRC_RATE, (unsigned)g_decl_rate);
        g_sink.rate = SLES_FORCE_SRC_RATE;
    }
    g_sink.bits = fmt->bitsPerSample;
    if (g_sink.bits != 16)
        l_warn("sles: bitsPerSample=%u, solo se soportan 16 bits", (unsigned)g_sink.bits);

    g_player_obj.vt = &object_vt;
    *pPlayer = (SLObjectItf)&g_player_obj;
    return SL_RESULT_SUCCESS;
}

static SLresult engine_create_output_mix(SLEngineItf self, SLObjectItf *pMix, SLuint32 n,
                                         const SLInterfaceID *pIids, const SLboolean *pRequired) {
    (void)self; (void)n; (void)pIids; (void)pRequired;
    if (pMix) *pMix = (SLObjectItf)&g_engine_obj;   /* placeholder válido */
    return SL_RESULT_SUCCESS;
}

static const struct SLEngineItf_ engine_vt = {
    .CreateAudioPlayer = engine_create_audio_player,
    .CreateOutputMix = engine_create_output_mix,
};

SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions, const SLEngineOption *pEngineOptions,
                        SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
                        const SLboolean *pInterfaceRequired) {
    (void)numOptions; (void)pEngineOptions; (void)numInterfaces; (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pEngine)
        return SL_RESULT_PARAMETER_INVALID;
    g_engine_obj.vt = &object_vt;
    g_engine_itf.vt = &engine_vt;
    *pEngine = (SLObjectItf)&g_engine_obj;
    return SL_RESULT_SUCCESS;
}

/* ---- PLAY ---- */
static SLresult play_set_state(SLPlayItf self, SLuint32 state) {
    (void)self;
    g_sink.playState = state;
    if (state == SL_PLAYSTATE_PLAYING && !g_sink.thread_started) {
        if (g_sink.port < 0)
            return SL_RESULT_PRECONDITIONS_VIOLATED;   /* no realizado */
        g_sink.run = 1;
        if (pthread_create(&g_sink.thread, NULL, audio_thread, NULL) == 0)
            g_sink.thread_started = 1;
        else {
            g_sink.run = 0;
            l_error("sles: no se pudo crear el hilo de audio");
            return SL_RESULT_INTERNAL_ERROR;
        }
    }
    return SL_RESULT_SUCCESS;
}
static SLresult play_get_state(SLPlayItf self, SLuint32 *pState) {
    (void)self;
    if (pState) *pState = g_sink.playState;
    return SL_RESULT_SUCCESS;
}
static SLresult play_get_duration(SLPlayItf self, SLmillisecond *pMs) {
    (void)self;
    if (pMs) *pMs = SL_TIME_UNKNOWN;
    return SL_RESULT_SUCCESS;
}
static SLresult play_get_position(SLPlayItf self, SLmillisecond *pMs) {
    (void)self;
    if (pMs) *pMs = 0;
    return SL_RESULT_SUCCESS;
}
static SLresult play_register_callback(SLPlayItf self, slPlayCallback cb, void *ctx) {
    (void)self; (void)cb; (void)ctx;
    return SL_RESULT_SUCCESS;   /* eventos de reproducción no usados */
}
static SLresult play_set_mask(SLPlayItf self, SLuint32 mask) { (void)self; (void)mask; return SL_RESULT_SUCCESS; }
static SLresult play_get_mask(SLPlayItf self, SLuint32 *pMask) { (void)self; if (pMask) *pMask = 0; return SL_RESULT_SUCCESS; }

static const struct SLPlayItf_ play_vt = {
    .SetPlayState = play_set_state,
    .GetPlayState = play_get_state,
    .GetDuration = play_get_duration,
    .GetPosition = play_get_position,
    .RegisterCallback = play_register_callback,
    .SetCallbackEventsMask = play_set_mask,
    .GetCallbackEventsMask = play_get_mask,
};

/* ---- BUFFER QUEUE (Android y estándar comparten la forma) ---- */
static SLresult bq_enqueue(SLAndroidSimpleBufferQueueItf self, const void *pBuffer, SLuint32 size) {
    (void)self;
    if (!pBuffer || size == 0)
        return SL_RESULT_PARAMETER_INVALID;
    pthread_mutex_lock(&g_sink.mtx);
    SLresult r = SL_RESULT_SUCCESS;
    if (g_sink.q_count >= SLES_QUEUE_MAX) {
        r = SL_RESULT_BUFFER_INSUFFICIENT;
    } else {
        int tail = (g_sink.q_head + g_sink.q_count) % SLES_QUEUE_MAX;
        g_sink.q_ptr[tail] = pBuffer;
        g_sink.q_size[tail] = size;
        g_sink.q_count++;
    }
    pthread_mutex_unlock(&g_sink.mtx);
    return r;
}
static SLresult bq_clear(SLAndroidSimpleBufferQueueItf self) {
    (void)self;
    pthread_mutex_lock(&g_sink.mtx);
    g_sink.q_head = 0;
    g_sink.q_count = 0;
    g_sink.cur_size = g_sink.cur_off = 0;
    pthread_mutex_unlock(&g_sink.mtx);
    return SL_RESULT_SUCCESS;
}
static SLresult bq_get_state(SLAndroidSimpleBufferQueueItf self, SLAndroidSimpleBufferQueueState *pState) {
    (void)self;
    if (pState) {
        pthread_mutex_lock(&g_sink.mtx);
        pState->count = g_sink.q_count;
        pState->index = 0;
        pthread_mutex_unlock(&g_sink.mtx);
    }
    return SL_RESULT_SUCCESS;
}
static SLresult bq_register_callback(SLAndroidSimpleBufferQueueItf self,
                                     slAndroidSimpleBufferQueueCallback cb, void *ctx) {
    g_sink.cb = (void *)cb;
    g_sink.cb_ctx = ctx;
    g_sink.cb_self = self;
    g_sink.callback_set = 1;
    return SL_RESULT_SUCCESS;
}

static const struct SLAndroidSimpleBufferQueueItf_ bq_vt = {
    .Enqueue = bq_enqueue,
    .Clear = bq_clear,
    .GetState = bq_get_state,
    .RegisterCallback = bq_register_callback,
};

/* ---- VOLUME (solo guarda el valor) ---- */
static SLresult vol_set_level(SLVolumeItf self, SLmillibel level) {
    (void)self;
    g_sink.volume = (SLuint32)level;
    return SL_RESULT_SUCCESS;
}
static SLresult vol_get_level(SLVolumeItf self, SLmillibel *pLevel) {
    (void)self;
    if (pLevel) *pLevel = (SLmillibel)g_sink.volume;
    return SL_RESULT_SUCCESS;
}
static SLresult vol_set_mute(SLVolumeItf self, SLboolean mute) { (void)self; (void)mute; return SL_RESULT_SUCCESS; }
static SLresult vol_get_mute(SLVolumeItf self, SLboolean *pMute) { (void)self; if (pMute) *pMute = SL_BOOLEAN_FALSE; return SL_RESULT_SUCCESS; }

static const struct SLVolumeItf_ vol_vt = {
    .SetVolumeLevel = vol_set_level,
    .GetVolumeLevel = vol_get_level,
    .SetMute = vol_set_mute,
    .GetMute = vol_get_mute,
};

/* Los punteros de vtable se asignan aquí porque los structs son const en tiempo de
 * compilación y el tipo de la interfaz se conoce solo al enlazar. */
__attribute__((constructor)) static void sles_init_vtables(void) {
    g_play_itf.vt = &play_vt;
    g_bq_itf.vt = &bq_vt;
    g_vol_itf.vt = &vol_vt;
    pthread_mutex_init(&g_sink.mtx, NULL);
}
