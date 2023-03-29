#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <wasm3.h>
#include <m3_env.h>

#include "loader.h"
#include "platform.h"
#include "libretro.h"

static retro_input_state_t input_state_cb;
static retro_input_poll_t input_poll_cb;
static retro_video_refresh_t video_cb;
static retro_environment_t environ_cb;
retro_audio_sample_batch_t audio_cb;

static uint8_t *pic;
static M3Environment* env;
static M3Runtime* runtime;
static M3Module* module;
static M3Function* upd;
static M3Function* load_uw8;

void
retro_init(void)
{
}

void
retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = "uw8";
	info->library_version = "1.0";
	info->need_fullpath = false;
	info->valid_extensions = "uw8";
}

void
retro_get_system_av_info(struct retro_system_av_info *info)
{
	info->timing.fps = 60.0;
	info->timing.sample_rate = 44100.0;

	info->geometry.base_width = 320;
	info->geometry.base_height = 240;
	info->geometry.max_width = 320;
	info->geometry.max_height = 240;
	info->geometry.aspect_ratio = 4.0 / 3.0;
}

unsigned
retro_api_version(void)
{
	return RETRO_API_VERSION;
}

static void check (M3Result result) {
    if (result != m3Err_none) {
        M3ErrorInfo info;
        m3_GetErrorInfo(runtime, &info);
        fprintf(stderr, "WASM error: %s (%s)\n", result, info.message);
        exit(1);
    }
}

bool
retro_load_game(const struct retro_game_info *game)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;
	
	pic = malloc(320 * 240 * 256);

	env = m3_NewEnvironment();
	uint32_t wasm3StackSize = 64 * 1024;
	runtime = m3_NewRuntime(env, wasm3StackSize, NULL);
	runtime->memory.maxPages = 4;
	check(ResizeMemory(runtime, 4));

	uint8_t* memory = m3_GetMemory(runtime, NULL, 0);

	check(m3_ParseModule(env, &module, loader, sizeof(loader)));
	module->memoryImported = true;
	check(m3_LoadModule(runtime, module));

	memcpy(memory, platform, sizeof(platform));
	check(m3_FindFunction(&load_uw8, runtime, "load_uw8"));
	check(m3_CallV(load_uw8, sizeof(platform)));
	uint32_t platformsize;
	m3_GetResultsV(load_uw8, &platformsize);
	printf("size: %u\n", platformsize);

	memcpy(memory, (uint8_t*)game->data, game->size);
	check(m3_CallV(load_uw8, game->size));
	uint32_t gamesize;
	m3_GetResultsV(load_uw8, &gamesize);
	printf("size: %u\n", gamesize);

	check(m3_FindFunction(&upd, runtime, "upd"));

	check(m3_RunStart(module));


	return true;
}

void
retro_run(void)
{
	input_poll_cb();

	m3_CallV(upd);
}

void
retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll_cb = cb;
}

void
retro_set_input_state(retro_input_state_t cb)
{
	input_state_cb = cb;
}

void
retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

void
retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;
}

void
retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_cb = cb;
}

void
retro_reset(void)
{
}

size_t
retro_serialize_size(void)
{
	return 0;
}

bool
retro_serialize(void *data, size_t size)
{
	return false;
}

bool
retro_unserialize(const void *data, size_t size)
{
	return false;
}

void retro_deinit(void) {
	m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
}

unsigned retro_get_region(void) {
	return RETRO_REGION_NTSC;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {}
size_t retro_get_memory_size(unsigned id) { return 0; }
void * retro_get_memory_data(unsigned id) { return NULL; }
void retro_unload_game(void) {}
void retro_set_audio_sample(retro_audio_sample_t cb) {}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }
