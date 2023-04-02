#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <wasm3.h>
#include <m3_env.h>

#include "loader.h"
#include "platform.h"
#include "libretro.h"

static retro_input_state_t input_state_cb;
static retro_input_poll_t input_poll_cb;
static retro_video_refresh_t video_cb;
static retro_environment_t environ_cb;
retro_audio_sample_t audio_cb;

uint32_t frameNumber = 0;
uint32_t sampleIndex = 0;
IM3Environment env;
static M3Runtime* main_runtime;
static M3Runtime* audio_runtime;
uint8_t* memory;
static M3Function* upd;
static M3Function* sndGes;
static M3Function* endFrame;

void
retro_init(void)
{
}

void
retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = "uw8";
	info->library_version = "0.2.2";
	info->need_fullpath = false;
	info->valid_extensions = "uw8|wasm";
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

void
verifyM3(IM3Runtime runtime, M3Result result)
{
	if (result != m3Err_none)
	{
		M3ErrorInfo info;
		m3_GetErrorInfo(runtime, &info);
		fprintf(stderr, "WASM error: %s (%s)\n", result, info.message);
		exit(1);
	}
}

m3ApiRawFunction(math1)
{
	m3ApiReturnType(float);
	m3ApiGetArg(float, v);
	*raw_return = ((float(*)(float))_ctx->userdata)(v);
	m3ApiSuccess();
}

m3ApiRawFunction(math2)
{
	m3ApiReturnType(float);
	m3ApiGetArg(float, a);
	m3ApiGetArg(float, b);
	*raw_return = ((float(*)(float, float))_ctx->userdata)(a, b);
	m3ApiSuccess();
}

m3ApiRawFunction(nopFunc)
{
	m3ApiSuccess();
}

void uw8_rect(int32_t dx, int32_t dy, int32_t w, int32_t h, uint8_t c) {
    uint32_t x = 0;
    uint32_t y = 0;
    int32_t x2 = dx;
    int32_t y2 = dy;

    while (y < h) {
        int32_t o_x = x2;
        int32_t o_y = y2;

        if (o_x >= 0 && o_x < 320 && o_y >= 0 && o_y < 240)
            memory[120 + o_x + o_y * 320] = c;

        x++;
        x2++;

        if (x >= w) {
            x = 0;
            x2 = dx;
            y++;
            y2++;
        }
    }
}

m3ApiRawFunction(rect)
{
	m3ApiGetArg(int32_t, dx);
	m3ApiGetArg(int32_t, dy);
	m3ApiGetArg(int32_t, w);
	m3ApiGetArg(int32_t, h);
	m3ApiGetArg(uint8_t, c);
	uw8_rect(dx, dy, w, h, c);
	m3ApiSuccess();
}

void uw8_blit(const uint8_t* spr, int32_t dx, int32_t dy, uint8_t trans, bool flip) {
    uint32_t x = 0;
    uint32_t y = 0;
    int32_t x2 = dx;
    int32_t y2 = dy;

    while (y < 16) {
        uint8_t c = flip ? spr[15 - x + y * 16] : spr[x + y * 16];
        int32_t o_x = x2;
        int32_t o_y = y2;

        if (c != trans and o_x >= 0 and o_x < 320 and o_y >= 0 and o_y < 240)
            memory[120 + o_x + o_y * 320] = c;

        x++;
        x2++;

        if (x >= 16) {
            x = 0;
            x2 = dx;
            y++;
            y2++;
        }
    }
}

m3ApiRawFunction(blit)
{
	m3ApiGetArgMem(const uint8_t*, spr);
	m3ApiGetArg(int32_t, dx);
	m3ApiGetArg(int32_t, dy);
	m3ApiGetArg(uint8_t, trans);
	m3ApiGetArg(bool, flip);
	uw8_blit(spr, dx, dy, trans, flip);
	m3ApiSuccess();
}

void
linkSystemFunctions(IM3Runtime runtime, IM3Module mod)
{
	m3_LinkRawFunctionEx(mod, "env", "acos", "f(f)", math1, acosf);
	m3_LinkRawFunctionEx(mod, "env", "asin", "f(f)", math1, asinf);
	m3_LinkRawFunctionEx(mod, "env", "atan", "f(f)", math1, atanf);
	m3_LinkRawFunctionEx(mod, "env", "atan2", "f(ff)", math2, atan2f);
	m3_LinkRawFunctionEx(mod, "env", "cos", "f(f)", math1, cosf);
	m3_LinkRawFunctionEx(mod, "env", "exp", "f(f)", math1, expf);
	m3_LinkRawFunctionEx(mod, "env", "log", "f(f)", math1, logf);
	m3_LinkRawFunctionEx(mod, "env", "sin", "f(f)", math1, sinf);
	m3_LinkRawFunctionEx(mod, "env", "tan", "f(f)", math1, tanf);
	m3_LinkRawFunctionEx(mod, "env", "pow", "f(ff)", math2, powf);

	m3_LinkRawFunction(mod, "env", "logChar", "v(i)", nopFunc);

	m3_LinkRawFunction(mod, "env", "rect", "v(iiiii)", rect);
	m3_LinkRawFunction(mod, "env", "blit", "v(iiiii)", blit);

	for(int i = 11; i < 64; ++i)
	{
		char name[128];
		sprintf(name, "reserved%d", i);
		m3_LinkRawFunction(mod, "env", name, "v()", nopFunc);
	}
}

m3ApiRawFunction(platformTrampoline)
{
	IM3Function func = (IM3Function)_ctx->userdata;
	uint32_t retCount = m3_GetRetCount(func);
	uint32_t argCount = m3_GetArgCount(func);
	const void* args[16];
	for(uint32_t i = 0; i < argCount; ++i)
		args[i] = &_sp[retCount + i];
	verifyM3(runtime, m3_Call(func, m3_GetArgCount(func), args));
	for(uint32_t i = 0; i < retCount; ++i)
		args[i] = &_sp[i];
	verifyM3(runtime, m3_GetResults(func, retCount, args));
	m3ApiSuccess();
}

void appendType(char* signature, M3ValueType type)
{
	if(type == c_m3Type_i32)
		strcat(signature, "i");
	else if(type == c_m3Type_i64)
		strcat(signature, "l");
	else if(type == c_m3Type_f32)
		strcat(signature, "f");
	else
	{
		fprintf(stderr, "Unsupported platform type %d\n", type);
		exit(1);
	}
}

void linkPlatformFunctions(IM3Runtime runtime, IM3Module cartMod, IM3Module platformMod)
{
	for(u32 functionIndex = 0; functionIndex < platformMod->numFunctions; ++functionIndex)
	{
		M3Function function = platformMod->functions[functionIndex];
		if(function.export_name != NULL)
		{
			IM3Function iFunc;
			verifyM3(runtime, m3_FindFunction(&iFunc, runtime, function.export_name));
			char signature[128] = { 0 };
			if(m3_GetRetCount(iFunc) > 0)
				appendType(signature, m3_GetRetType(iFunc, 0));
			else
				strcat(signature, "v");
			strcat(signature, "(");
			for(uint32_t i = 0; i < m3_GetArgCount(iFunc); ++i)
				appendType(signature, m3_GetArgType(iFunc, i));
			strcat(signature, ")");
			m3_LinkRawFunctionEx(cartMod, "env", function.export_name, signature, platformTrampoline, iFunc);
		}
	}
}

void*
loadUw8(uint32_t* sizeOut, IM3Runtime runtime, IM3Function loadFunc, uint8_t* memory, const unsigned char* uw8, size_t uw8Size)
{
	memcpy(memory, uw8, uw8Size);
	verifyM3(runtime, m3_CallV(loadFunc, (uint32_t)uw8Size));
	verifyM3(runtime, m3_GetResultsV(loadFunc, sizeOut));
	void* wasm = malloc(*sizeOut);
	memcpy(wasm, memory, *sizeOut);
	return wasm;
}

bool
retro_load_game(const struct retro_game_info *game)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;

	env = m3_NewEnvironment();

	main_runtime = m3_NewRuntime(env, 16384, NULL);
	main_runtime->memory.maxPages = 4;
	verifyM3(main_runtime, ResizeMemory(main_runtime, 4));

	audio_runtime = m3_NewRuntime(env, 16384, NULL);
	audio_runtime->memory.maxPages = 4;
	verifyM3(audio_runtime, ResizeMemory(audio_runtime, 4));

	memory = m3_GetMemory(main_runtime, NULL, 0);
	assert(memory != NULL);

	IM3Module loader_mod;
	verifyM3(main_runtime, m3_ParseModule(env, &loader_mod, loader, sizeof(loader)));
	loader_mod->memoryImported = true;
	verifyM3(main_runtime, m3_LoadModule(main_runtime, loader_mod));
	verifyM3(main_runtime, m3_CompileModule(loader_mod));
	verifyM3(main_runtime, m3_RunStart(loader_mod));

	IM3Function load_func;
	verifyM3(main_runtime, m3_FindFunction(&load_func, main_runtime, "load_uw8"));

	uint32_t platform_size;
	void* platform_wasm = loadUw8(&platform_size, main_runtime, load_func, memory, platform, sizeof(platform));

	uint32_t cart_size;
	void* cart_wasm = loadUw8(&cart_size, main_runtime, load_func, memory, game->data, game->size);

	IM3Module main_platform_mod;
	verifyM3(main_runtime, m3_ParseModule(env, &main_platform_mod, platform_wasm, platform_size));
	main_platform_mod->memoryImported = true;
	verifyM3(main_runtime, m3_LoadModule(main_runtime, main_platform_mod));
	linkSystemFunctions(main_runtime, main_platform_mod);
	verifyM3(main_runtime, m3_CompileModule(main_platform_mod));
	verifyM3(main_runtime, m3_RunStart(main_platform_mod));

	IM3Module audio_platform_mod;
	verifyM3(main_runtime, m3_ParseModule(env, &audio_platform_mod, platform_wasm, platform_size));
	audio_platform_mod->memoryImported = true;
	verifyM3(audio_runtime, m3_LoadModule(audio_runtime, audio_platform_mod));
	linkSystemFunctions(audio_runtime, audio_platform_mod);
	verifyM3(audio_runtime, m3_CompileModule(audio_platform_mod));
	verifyM3(audio_runtime, m3_RunStart(audio_platform_mod));

	IM3Module main_cart_mod;
	verifyM3(main_runtime, m3_ParseModule(env, &main_cart_mod, cart_wasm, cart_size));
	main_platform_mod->memoryImported = true;
	verifyM3(main_runtime, m3_LoadModule(main_runtime, main_cart_mod));
	linkSystemFunctions(main_runtime, main_cart_mod);
	linkPlatformFunctions(main_runtime, main_cart_mod, main_platform_mod);
	verifyM3(main_runtime, m3_CompileModule(main_cart_mod));
	verifyM3(main_runtime, m3_RunStart(main_cart_mod));

	IM3Module audio_cart_mod;
	verifyM3(audio_runtime, m3_ParseModule(env, &audio_cart_mod, cart_wasm, cart_size));
	main_platform_mod->memoryImported = true;
	verifyM3(audio_runtime, m3_LoadModule(audio_runtime, audio_cart_mod));
	linkSystemFunctions(audio_runtime, audio_cart_mod);
	linkPlatformFunctions(audio_runtime, audio_cart_mod, main_platform_mod);
	verifyM3(audio_runtime, m3_CompileModule(audio_cart_mod));
	verifyM3(audio_runtime, m3_RunStart(audio_cart_mod));

	verifyM3(main_runtime, m3_FindFunction(&upd, main_runtime, "upd"));
	verifyM3(audio_runtime, m3_FindFunction(&sndGes, audio_runtime, "sndGes"));
	verifyM3(main_runtime, m3_FindFunction(&endFrame, main_runtime, "endFrame"));

	return true;
}

static const uint8_t retro_bind[] = {
	[RETRO_DEVICE_ID_JOYPAD_UP] = 1<<0,
	[RETRO_DEVICE_ID_JOYPAD_DOWN] = 1<<1,
	[RETRO_DEVICE_ID_JOYPAD_LEFT] = 1<<2,
	[RETRO_DEVICE_ID_JOYPAD_RIGHT] = 1<<3,
	[RETRO_DEVICE_ID_JOYPAD_B] = 1<<4,
	[RETRO_DEVICE_ID_JOYPAD_A] = 1<<5,
	[RETRO_DEVICE_ID_JOYPAD_Y] = 1<<6,
	[RETRO_DEVICE_ID_JOYPAD_X] = 1<<7,
};

void
retro_run(void)
{
	input_poll_cb();

	memory[0x00044] = 0;
	for (int i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
		if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i))
			memory[0x00044] ^= retro_bind[i];

	verifyM3(main_runtime, m3_CallV(upd));

	uint32_t* palette = (uint32_t*)&memory[0x13000];
	uint8_t* pixels = memory + 0x00078;
	uint32_t pic[320*240];

	for (int i = 0; i < 320*240; i++)
	{
		uint32_t c = palette[pixels[i]];
		pic[i] = (c & 0xff00ff00) | ((c & 0xff) << 16) | ((c >> 16) & 0xff);
	}

	video_cb(&pic, 320, 240, 320*sizeof(uint32_t));

	uint8_t* audio_memory = m3_GetMemory(audio_runtime, NULL, 0);
	memcpy(audio_memory + 0x00050, memory + 0x00050, 32);

	for(int i = 0; i < 44100/60; ++i) {
		float_t left = 0;
		verifyM3(audio_runtime, m3_CallV(sndGes, ++sampleIndex));
		verifyM3(audio_runtime, m3_GetResultsV(sndGes, &left));

		float_t right = 0;
		verifyM3(audio_runtime, m3_CallV(sndGes, ++sampleIndex));
		verifyM3(audio_runtime, m3_GetResultsV(sndGes, &right));

		audio_cb((int16_t)(left * 32767.0f), (int16_t)(right * 32767.0f));
	}

	verifyM3(main_runtime, m3_CallV(endFrame));

	*(uint32_t*)&memory[0x00040] = frameNumber++ * 1000 / 60 + 8;
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
retro_set_audio_sample(retro_audio_sample_t cb)
{
	audio_cb = cb;
}

void
retro_reset(void)
{
	frameNumber = 0;
	sampleIndex = 0;
	// TODO
}

size_t
retro_serialize_size(void)
{
	return 1 << 18;
}

bool
retro_serialize(void *data, size_t size)
{
	memcpy(data, memory, 1 << 18);
	return true;
}

bool
retro_unserialize(const void *data, size_t size)
{
	memcpy(memory, data, 1 << 18);
	return true;
}

void
retro_deinit(void) {
	frameNumber = 0;
	sampleIndex = 0;
	m3_FreeRuntime(main_runtime);
	m3_FreeRuntime(audio_runtime);
	m3_FreeEnvironment(env);
}

unsigned
retro_get_region(void) {
	return RETRO_REGION_NTSC;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {}
size_t retro_get_memory_size(unsigned id) { return 0; }
void * retro_get_memory_data(unsigned id) { return NULL; }
void retro_unload_game(void) {}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }
