#include "libretro.h"
#include "../towns/outside_world/headless_mode.h"
#include "../towns/townsthread.h"
#include "../towns/towns.h"
#include "../main_cui/argv/townsargv.h"

#include <string>
#include <cstring>
#include <vector>

// ── Libretro callbacks (set by frontend) ──────────────────────────
static retro_video_refresh_t         video_cb        = nullptr;
static retro_audio_sample_batch_t    audio_batch_cb  = nullptr;
static retro_input_poll_t            input_poll_cb   = nullptr;
static retro_input_state_t           input_state_cb  = nullptr;
static retro_environment_t           environ_cb      = nullptr;
static retro_log_printf_t            log_cb          = nullptr;

// ── Custom Sound Connection ───────────────────────────────────────
class LibretroSound : public Outside_World::Sound
{
public:
    std::vector<int16_t> pcm_buffer;

    void Start(void) override {}
    void Stop(void) override {}
    void Polling(void) override {}

    void CDDAPlay(const DiscImage &discImg,DiscImage::MinSecFrm from,DiscImage::MinSecFrm to,bool repeat,unsigned int,unsigned int) override {}
    void CDDASetVolume(float leftVol,float rightVol) override {}
    void CDDAStop(void) override {}
    void CDDAPause(void) override {}
    void CDDAResume(void) override {}
    bool CDDAIsPlaying(void) override { return false; }
    DiscImage::MinSecFrm CDDACurrentPosition(void) override
    {
        DiscImage::MinSecFrm msf;
        msf.FromHSG(0);
        return msf;
    }

    void FMPCMPlay(std::vector<unsigned char> &wave) override
    {
        size_t nsamples = wave.size() / 2;
        const int16_t *src = reinterpret_cast<const int16_t*>(wave.data());
        pcm_buffer.insert(pcm_buffer.end(), src, src + nsamples);
    }

    void FMPCMPlayStop(void) override {}
    bool FMPCMChannelPlaying(void) override { return false; }

    void BeepPlay(int samplingRate, std::vector<unsigned char> &wave) override {}
    void BeepPlayStop(void) override {}
    bool BeepChannelPlaying(void) const override { return false; }
};

// ── Emulator state ────────────────────────────────────────────────
static FMTownsWithMediumFidelityCPU *g_towns  = nullptr;
static Headless_Mode                *g_world  = nullptr;
static LibretroSound                *g_sound  = nullptr;
static TownsThread                   g_thread;
static std::string                   g_system_dir;
static std::string                   g_save_dir;
static std::string                   g_disc_path;
static bool                          g_loaded = false;

// Forward declaration
static void towns_cleanup();

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
    g_sound = nullptr;
    g_loaded = false;
    if (log_cb) log_cb(RETRO_LOG_INFO, "[TOWNSEMU] retro_init\n");
}

static void towns_cleanup()
{
    if (g_towns)
    {
        delete g_towns;
        g_towns = nullptr;
    }
    if (g_world)
    {
        delete g_world;
        g_world = nullptr;
    }
    if (g_sound)
    {
        delete g_sound;
        g_sound = nullptr;
    }
    g_loaded = false;
}

RETRO_API void retro_deinit(void)
{
    towns_cleanup();
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
    if (!game || !game->path)
    {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "[TOWNSEMU] No disc image provided\n");
        return false;
    }

    // ── 1. Get system & save directories from frontend ──────────────
    const char *sys_dir = nullptr;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys_dir) || !sys_dir)
    {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "[TOWNSEMU] Cannot get system directory\n");
        return false;
    }
    g_system_dir = std::string(sys_dir) + "/fmtowns";

    const char *sav_dir = nullptr;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sav_dir) && sav_dir)
        g_save_dir = std::string(sav_dir);
    else
        g_save_dir = g_system_dir; // fallback

    g_disc_path  = game->path;

    if (log_cb) log_cb(RETRO_LOG_INFO,
        "[TOWNSEMU] system dir : %s\n", g_system_dir.c_str());
    if (log_cb) log_cb(RETRO_LOG_INFO,
        "[TOWNSEMU] save dir   : %s\n", g_save_dir.c_str());
    if (log_cb) log_cb(RETRO_LOG_INFO,
        "[TOWNSEMU] disc image : %s\n", g_disc_path.c_str());

    // ── 2. Pixel format ────────────────────────────────────────────
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        if (log_cb) log_cb(RETRO_LOG_ERROR, "[TOWNSEMU] XRGB8888 not supported\n");
        return false;
    }

    // ── 3. Build argv for TOWNSEMU ─────────────────────────────────
    std::string cmos_path = g_save_dir + "/fmtowns.cmos";
    std::vector<std::string> args = {
        "townsemu_libretro",   // argv[0] placeholder
        g_system_dir,          // ROM directory
        "-CD", g_disc_path,    // disc image
        "-CMOS", cmos_path,    // CMOS file
        "-NOWAITBOOT"          // Faster boot
    };

    std::vector<const char*> cargs;
    for (auto &s : args) cargs.push_back(s.c_str());

    TownsARGV argv;
    argv.AnalyzeCommandParameter((int)cargs.size(), const_cast<char**>(cargs.data()));

    // ── 4. Create emulator objects ─────────────────────────────────
    towns_cleanup(); // safety: clean previous session if any

    g_world = new Headless_Mode();
    g_towns = new FMTownsWithMediumFidelityCPU();
    g_sound = new LibretroSound();

    g_towns->sound.SetOutsideWorld(g_sound);
    g_towns->sound.SetCDROMPointer(&g_towns->cdrom);
    g_towns->sound.SetSCSIPointer(&g_towns->scsi);

    // ── 5. Setup emulator & load BIOS / mount disc ────────────────
    if (!FMTownsCommon::Setup(*g_towns, g_world, nullptr, argv))
    {
        if (log_cb) log_cb(RETRO_LOG_ERROR,
            "[TOWNSEMU] Failed to load BIOS from: %s\n", g_system_dir.c_str());
        towns_cleanup();
        return false;
    }

    // ── 6. Initialize hardware ─────────────────────────────────────
    g_towns->PowerOn();

    g_loaded = true;
    if (log_cb) log_cb(RETRO_LOG_INFO, "[TOWNSEMU] Emulator ready\n");
    return true;
}

RETRO_API void retro_unload_game(void)
{
    towns_cleanup();
    if (log_cb) log_cb(RETRO_LOG_INFO, "[TOWNSEMU] retro_unload_game\n");
}

RETRO_API void retro_run(void)
{
    if (!g_loaded || !g_towns || !g_world) return;

    // ── 1. Input ───────────────────────────────────────────────────
    if (input_poll_cb) input_poll_cb();

    if (input_state_cb)
    {
        auto btn = [&](unsigned id) -> bool {
            return input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
        };

        bool up    = btn(RETRO_DEVICE_ID_JOYPAD_UP);
        bool down  = btn(RETRO_DEVICE_ID_JOYPAD_DOWN);
        bool left  = btn(RETRO_DEVICE_ID_JOYPAD_LEFT);
        bool right = btn(RETRO_DEVICE_ID_JOYPAD_RIGHT);
        bool fire1 = btn(RETRO_DEVICE_ID_JOYPAD_A) || btn(RETRO_DEVICE_ID_JOYPAD_B);
        bool fire2 = btn(RETRO_DEVICE_ID_JOYPAD_X) || btn(RETRO_DEVICE_ID_JOYPAD_Y);

        g_towns->SetGamePadState(0, fire1, fire2, left, right, up, down, false, false, false);
    }

    // ── 2. Run one frame ───────────────────────────────────────────
    // Run CPU & scheduled tasks for ~16.666ms (1/60th second in townsTime nanoseconds)
    uint64_t target_time = g_towns->state.townsTime + 16666666;
    while (g_towns->state.townsTime < target_time)
    {
        while (g_towns->state.townsTime <= g_towns->state.nextFastDevicePollingTime && 0 == g_towns->GetStopFlags())
        {
            g_towns->RunOneInstruction();
            g_towns->pic.ProcessIRQ(g_towns->CPU(), g_towns->mem);
        }

        g_towns->RunScheduledTasks();
        g_towns->RunFastDevicePolling();

        if (0 != g_towns->GetStopFlags())
        {
            if (g_towns->CheckAbort()) break;
        }
    }

    g_towns->ProcessSound(g_world);
    g_towns->cdrom.UpdateCDDAState(g_towns->state.townsTime);

    // ── 3. Video ───────────────────────────────────────────────────
    if (video_cb)
    {
        TownsRender render;
        render.Prepare(g_towns->crtc);
        render.damperWireLine = g_towns->var.damperWireLine;
        render.BuildImage(g_towns->GetUsingVRAM(), g_towns->crtc.GetPalette(), g_towns->crtc.chaseHQPalette);

        auto img = render.GetImage();
        if (img.wid > 0 && img.hei > 0)
        {
            video_cb(
                img.rgba,
                img.wid,
                img.hei,
                img.wid * 4
            );
        }
    }

    // ── 4. Audio ───────────────────────────────────────────────────
    if (audio_batch_cb && g_sound)
    {
        if (!g_sound->pcm_buffer.empty())
        {
            size_t nframes = g_sound->pcm_buffer.size() / 2;
            audio_batch_cb(g_sound->pcm_buffer.data(), nframes);
            g_sound->pcm_buffer.clear();
        }
        else
        {
            // 735 stereo frames of silence for 1/60s frame at 44100Hz
            static const int16_t silence[1470] = {0};
            audio_batch_cb(silence, 735);
        }
    }
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
