/*
Capture Checker
Copyright (C) <2025> <Janne Pitkänen> <acebanzkux@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <Windows.h>
#pragma comment(lib, "winmm.lib")
#endif
#include <chrono>
#include <thread>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define SETTING_BEEP_FILE_INFO "beep_info"
#define SETTING_VIDEO_TS_CHECK "video_ts_check"
#define SETTING_AUDIO_TS_CHECK "audio_ts_check"
#define SETTING_SOURCE_ENABLED_CHECK "source_enabled_check"
#define SETTING_SOURCE_ENABLED_TIME "source_enabled_time"
#define SETTING_TEST_BEEP "test_beep"

#define TEXT_BEEP_FILE_INFO \
	obs_module_text(    \
		"Place capture-checker.wav in the plugins folder (likely in C:\\Program Files\\obs-studio\\obs-plugins\\64bit) for custom alert sound.")
#define TEXT_VIDEO_TS_CHECK obs_module_text("Video timestamp check")
#define TEXT_AUDIO_TS_CHECK obs_module_text("Audio timestamp check")
#define TEXT_SOURCE_ENABLED_CHECK obs_module_text("Source enabled check")
#define TEXT_SOURCE_ENABLED_TIME obs_module_text("Source enabled time until check in seconds")
#define TEXT_TEST_BEEP obs_module_text("Test Alert Sound")

struct capture_checker_data {
	obs_source_t *context;
	obs_source_t *source;

	obs_data_t *settings;

	obs_source_frame *current_frame;
	obs_audio_data *current_audio;

	bool video_ts_check;
	bool audio_ts_check;
	bool source_enabled_check;
	uint16_t source_enabled_time;

	std::thread thread;
	bool thread_active;
	// How long since the frame has changed?

	signal_handler_t *signal_handler;
};

static const char *filter_name(void *)
{
	return obs_module_text("Capture Checker");
}

static void filter_update(void *data, obs_data_t *settings)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	bool new_video_ts_check = (bool)obs_data_get_bool(settings, SETTING_VIDEO_TS_CHECK);
	bool new_audio_ts_check = (bool)obs_data_get_bool(settings, SETTING_AUDIO_TS_CHECK);
	bool new_source_enabled_check = (bool)obs_data_get_bool(settings, SETTING_SOURCE_ENABLED_CHECK);

	uint16_t new_source_enabled_time = (uint16_t)obs_data_get_int(settings, SETTING_SOURCE_ENABLED_TIME);

	if (new_video_ts_check != filter->video_ts_check)
		filter->video_ts_check = new_video_ts_check;

	if (new_audio_ts_check != filter->audio_ts_check)
		filter->audio_ts_check = new_audio_ts_check;

	if (new_source_enabled_check != filter->source_enabled_check)
		filter->source_enabled_check = new_source_enabled_check;

	if (new_source_enabled_time != filter->source_enabled_time)
		filter->source_enabled_time = new_source_enabled_time;

	// TODO: Setting for how long the frame can be the same (ie. filter is getting frames with new timestamp but contents are not changing)
}

void thread_loop(void *data);

void start_thread(void *data)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	if (filter->thread_active || !obs_source_enabled(filter->context))
		return;

	filter->thread_active = true;
	filter->thread = std::thread(thread_loop, (void *)filter);
}

void end_thread(void *data)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	if (!filter->thread_active)
		return;

	filter->thread_active = false;

	filter->thread.join();

	filter->current_frame = nullptr;
	obs_log(LOG_INFO, "Thread ended");
}

static void filter_enabled(void *data, calldata_t *calldata)
{
	bool enabled = calldata_bool(calldata, "enabled");

	if (enabled)
		start_thread(data);
	else
		end_thread(data);
}

void frontend_event(obs_frontend_event event, void *data)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;
	if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		// TODO: try condition variable for stopping the thread when exiting OBS
		//filter->thread_active = false;
	}
}

static void *filter_create(obs_data_t *settings, obs_source_t *context)
{
	struct capture_checker_data *filter = (capture_checker_data *)bzalloc(sizeof(*filter));

	filter->context = context;
	filter_update(filter, settings);
	filter->source = nullptr;

	filter->settings = settings;

	filter->current_frame = nullptr;

	filter->signal_handler = obs_source_get_signal_handler(context);
	signal_handler_connect(filter->signal_handler, "enable", filter_enabled, filter);

	obs_frontend_add_event_callback(frontend_event, filter);

	return filter;
}

static void filter_destroy(void *data)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	signal_handler_disconnect(filter->signal_handler, "enable", filter_enabled, filter);

	end_thread(data);
	bfree(data);
}

void play_alert_sound()
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
	// TODO: Different sound files for different checks
	PlaySound((TEXT("../../obs-plugins/64bit/capture-checker.wav")), NULL, SND_FILENAME);
#endif
}

bool test_alert_sound(obs_properties_t *, obs_property_t *, void *)
{
	play_alert_sound();

	return true;
}

static obs_properties_t *filter_properties(void *data)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, SETTING_BEEP_FILE_INFO, TEXT_BEEP_FILE_INFO, OBS_TEXT_INFO);
	obs_properties_add_bool(props, SETTING_VIDEO_TS_CHECK, TEXT_VIDEO_TS_CHECK);
	obs_properties_add_bool(props, SETTING_AUDIO_TS_CHECK, TEXT_AUDIO_TS_CHECK);
	obs_properties_add_bool(props, SETTING_SOURCE_ENABLED_CHECK, TEXT_SOURCE_ENABLED_CHECK);
	obs_properties_add_int_slider(props, SETTING_SOURCE_ENABLED_TIME, TEXT_SOURCE_ENABLED_TIME, 1, 60 * 60, 1);
	obs_properties_add_button(props, SETTING_TEST_BEEP, TEXT_TEST_BEEP, test_alert_sound);

	return props;
}

void thread_loop(void *data)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	uint64_t frame_ts = 0;
	uint64_t audio_ts = 0;

	bool prev_visible = false;
	uint64_t not_visible_since_ts = 0;

	while (filter->thread_active) {
		if (filter->current_frame == nullptr) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			continue;
		}

		if (filter->video_ts_check && frame_ts - filter->current_frame->timestamp == 0) {
			obs_log(LOG_INFO, "Video timestamp check alert!");
			play_alert_sound();
		}

		// TODO: Check for difference in data of video and audio

		if (filter->audio_ts_check && audio_ts - filter->current_audio->timestamp == 0) {
			obs_log(LOG_INFO, "Audio timestamp check alert!");
			play_alert_sound();
		}

		bool current_visible = obs_source_active(filter->source);

		if (!current_visible && prev_visible)
			not_visible_since_ts = filter->current_frame->timestamp;

		if (filter->source_enabled_check && !current_visible &&
		    filter->current_frame->timestamp - not_visible_since_ts >
			    1000000000ULL * filter->source_enabled_time) {
			obs_log(LOG_INFO, "Source enabled check alert!");
			play_alert_sound();
		}

		// TODO: Video/Audio Desync check

		prev_visible = current_visible;

		frame_ts = filter->current_frame->timestamp;
		audio_ts = filter->current_audio->timestamp;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}

static struct obs_source_frame *filter_video(void *data, struct obs_source_frame *frame)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	if (filter->source == nullptr)
		filter->source = obs_filter_get_parent(filter->context);

	if (!filter->thread_active && obs_source_enabled(filter->context) && obs_source_active(filter->source))
		start_thread(data);

	if (filter->current_frame == nullptr || frame->timestamp != filter->current_frame->timestamp)
		filter->current_frame = frame;

	return frame;
}

static struct obs_audio_data *filter_audio(void *data, struct obs_audio_data *audio)
{
	struct capture_checker_data *filter = (capture_checker_data *)data;

	filter->current_audio = audio;

	return audio;
}

void filter_defaults(void *data, obs_data_t *settings)
{
	obs_data_set_default_bool(settings, SETTING_VIDEO_TS_CHECK, true);
	obs_data_set_default_bool(settings, SETTING_AUDIO_TS_CHECK, true);
	obs_data_set_default_bool(settings, SETTING_SOURCE_ENABLED_CHECK, true);
	obs_data_set_default_int(settings, SETTING_SOURCE_ENABLED_TIME, 5);
}

bool obs_module_load(void)
{
	struct obs_source_info filter_info = {};
	filter_info.id = "capture_checker_filter";
	filter_info.type = OBS_SOURCE_TYPE_FILTER;
	filter_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC | OBS_SOURCE_AUDIO;
	filter_info.get_name = filter_name;
	filter_info.create = filter_create;
	filter_info.destroy = filter_destroy;
	filter_info.update = filter_update;
	filter_info.get_defaults2 = filter_defaults;
	filter_info.get_properties = filter_properties;
	filter_info.filter_video = filter_video;
	filter_info.filter_audio = filter_audio;

	obs_register_source(&filter_info);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
