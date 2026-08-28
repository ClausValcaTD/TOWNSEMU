#include "libretro.h"
#include "../towns/outside_world/headless_mode.h"
#include "../towns/townsthread.h"
#include "../towns/towns.h"
#include "../main_cui/argv/townsargv.h"

#include <string>
#include <cstring>

// ── Libretro callbacks (set by frontend) ──────────────────────────
static retro_video_refresh_t         video_cb        = nullptr;
static retro_audio_sample_batch_t    audio_batch_cb  = nullptr;
static retro_input_poll_t            input_poll_cb   = nullptr;
static retro_input_state_t           input_state_cb  = nullptr;
static retro_environment_t           environ_cb      = nullptr;
static retro_log_printf_t            log_cb          = nullptr;

// ── Emulator state ────────────────────────────────────────────────
static FMTownsCommon  *g_towns  = nullptr;
static Headless_Mode  *g_world  = nullptr;
static bool            g_loaded = false;

// ─────────────────────────────────────────────────────────────────
// Required libretro API
// ─────────────────────────────────────────────────────────────────

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
    bool no_content = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

    retro_log_callback log = {};
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)    { video_cb       = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)      { /* unused */        }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb)          { input_poll_cb  = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb)        { input_state_cb = cb; }

RETRO_API void retro_init(void)
{
    g_towns = nullptr;
    g_world = nullptr;
    g_loaded = false;
    if (log_cb) log_cb(RETRO_LOG_INFO, "[TOWNSEMU] retro_init\n");
}

RETRO_API void retro_deinit(void)
{
    if (log_cb) log_cb(RETRO_LOG_INFO, "[TOWNSEMU] retro_deinit\n");
}

RETRO_API unsigned retro_api_version(void)   { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    std::memset(info, 0, sizeof(*info));
    info->library_name     = "TOWNSEMU";
    info->library_version  = "0.1.0";
    info->valid_extensions = "cue|iso|bin";
    info->need_fullpath    = true;   // we open the disc image ourselves
    info->block_extract    = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    std::memset(info, 0, sizeof(*info));
    // FM Towns native resolution — will be confirmed later
    info->geometry.base_width   = 640;
    info->geometry.base_height  = 480;
    info->geometry.max_width    = 640;
    info->geometry.max_height   = 480;
    info->geometry.aspect_ratio = 0.0f; // auto
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = 44100.0;
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
    if (log_cb) log_cb(RETRO_LOG_INFO, "[TOWNSEMU] retro_load_game: %s\n",
                       game ? game->path : "(null)");

    // Pixel format — we want XRGB8888
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "[TOWNSEMU] XRGB8888 not supported\n");
        return false;
    }

    g_loaded = true;
    return true;
}

RETRO_API void retro_unload_game(void)
{
    g_loaded = false;
    if (log_cb) log_cb(RETRO_LOG_INFO, "[TOWNSEMU] retro_unload_game\n");
}

RETRO_API void retro_run(void)
{
    if (!g_loaded) return;
    if (input_poll_cb) input_poll_cb();

    // TODO Issue #2: run one emulation frame here
    // TODO Issue #3: push framebuffer via video_cb
    // TODO Issue #5: push audio via audio_batch_cb
}

RETRO_API void retro_reset(void)                              { /* TODO */ }
RETRO_API size_t retro_serialize_size(void)                   { return 0;  }
RETRO_API bool retro_serialize(void*, size_t)                 { return false; }
RETRO_API bool retro_unserialize(const void*, size_t)         { return false; }
RETRO_API unsigned retro_get_region(void)                     { return RETRO_REGION_NTSC; }
RETRO_API void *retro_get_memory_data(unsigned)               { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned)              { return 0; }
RETRO_API void retro_cheat_reset(void)                        {}
RETRO_API void retro_cheat_set(unsigned,bool,const char*)     {}
RETRO_API bool retro_load_game_special(unsigned,
    const retro_game_info*, size_t)                           { return false; }
