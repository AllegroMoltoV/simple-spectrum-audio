#include "plugin-entry.h"

#include <clap/clap.h>
#include <clapwrapper/vst3.h>

#include <visage/app.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "sample-ring.h"
#include "spectrum-analyzer.h"
#include "spectrum-view.h"

#ifndef SIMPLE_SPECTRUM_VERSION
#define SIMPLE_SPECTRUM_VERSION "0.0.0"
#endif

namespace {

enum ParamId : clap_id { kResolution = 0, kRange = 1, kTilt = 2, kSmoothing = 3, kParamCount = 4 };

struct ParamInfo {
	clap_id id;
	const char *name;
	double min;
	double max;
	double defaultValue;
	clap_param_info_flags flags;
};

constexpr clap_param_info_flags kStepped = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

constexpr std::array<ParamInfo, kParamCount> kParams{{
	{kResolution, "Resolution", 0.0, 2.0, 1.0, kStepped},
	{kRange, "Range", 0.0, 2.0, 1.0, kStepped},
	{kTilt, "Tilt", 0.0, 2.0, 2.0, kStepped},
	{kSmoothing, "Smoothing", 0.0, 100.0, 50.0, CLAP_PARAM_IS_AUTOMATABLE},
}};

constexpr std::array<int, 3> kResolutions{2048, 4096, 8192};
constexpr std::array<double, 3> kFloors{-60.0, -90.0, -120.0};
constexpr std::array<double, 3> kTilts{0.0, 3.0, 4.5};
constexpr std::array<const char *, 3> kResolutionNames{"2048", "4096", "8192"};
constexpr std::array<const char *, 3> kRangeNames{"-60 dB", "-90 dB", "-120 dB"};
constexpr std::array<const char *, 3> kTiltNames{"0 dB/oct", "3 dB/oct", "4.5 dB/oct"};

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER,
				 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor kDescriptor = {
	CLAP_VERSION_INIT,
	"jp.allegromoltov.simple-spectrum",
	"Simple Spectrum",
	"AllegroMoltoV",
	"https://github.com/AllegroMoltoV/simple-spectrum-audio",
	"https://github.com/AllegroMoltoV/simple-spectrum-audio",
	"https://github.com/AllegroMoltoV/simple-spectrum-audio",
	SIMPLE_SPECTRUM_VERSION,
	"Shows the spectrum of the input and passes the audio through unchanged.",
	kFeatures,
};

constexpr uint32_t kStateMagic = 0x53535031; // "SSP1"
constexpr int kDefaultWidth = 640;
constexpr int kDefaultHeight = 320;
constexpr int kMinWidth = 400;
constexpr int kMinHeight = 200;

struct Plugin {
	clap_plugin plugin{};
	const clap_host *host = nullptr;
	const clap_host_posix_fd_support *hostFd = nullptr;
	// Values shared between the main thread, the audio thread and the GUI thread.
	std::array<std::atomic<double>, kParamCount> values{};
	std::atomic<double> sampleRate{48000.0};
	ssp::SampleRing ring;
	// GUI thread.
	std::unique_ptr<visage::ApplicationWindow> window;
	ssp::SpectrumAnalyzer analyzer;
	uint64_t ringCursor = 0;
	std::vector<float> scratch = std::vector<float>(ssp::SampleRing::kCapacity / 2);
	int registeredFd = -1;
};

Plugin *self(const clap_plugin *plugin)
{
	return static_cast<Plugin *>(plugin->plugin_data);
}

double clampParam(clap_id id, double value)
{
	const auto &info = kParams[id];
	value = std::clamp(value, info.min, info.max);
	return (info.flags & CLAP_PARAM_IS_STEPPED) ? std::round(value) : value;
}

size_t choice(const Plugin *p, ParamId id)
{
	return size_t(std::clamp(int(std::lround(p->values[id].load(std::memory_order_relaxed))), 0, 2));
}

ssp::DisplaySettings displaySettings(const Plugin *p)
{
	ssp::DisplaySettings s;
	s.resolution = kResolutions[choice(p, kResolution)];
	s.floorDb = kFloors[choice(p, kRange)];
	s.tiltDbPerOct = kTilts[choice(p, kTilt)];
	s.smoothing = p->values[kSmoothing].load(std::memory_order_relaxed) / 100.0;
	return s;
}

void applyEvents(Plugin *p, const clap_input_events *events)
{
	const uint32_t count = events->size(events);
	for (uint32_t i = 0; i < count; ++i) {
		const clap_event_header *header = events->get(events, i);
		if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
			continue;
		const auto *event = reinterpret_cast<const clap_event_param_value *>(header);
		if (event->param_id < kParamCount)
			p->values[event->param_id].store(clampParam(event->param_id, event->value),
							 std::memory_order_relaxed);
	}
}

// Sets flush-to-zero and denormals-are-zero for the scope of process, and restores the host's setting.
class DenormalGuard {
public:
	DenormalGuard()
	{
#if defined(__x86_64__) || defined(_M_X64)
		saved_ = _mm_getcsr();
		_mm_setcsr(saved_ | 0x8040);
#endif
	}
	~DenormalGuard()
	{
#if defined(__x86_64__) || defined(_M_X64)
		_mm_setcsr(saved_);
#endif
	}
	DenormalGuard(const DenormalGuard &) = delete;
	DenormalGuard &operator=(const DenormalGuard &) = delete;

private:
	unsigned saved_ = 0;
};

// --- clap_plugin ---

bool pluginInit(const clap_plugin *plugin)
{
	auto *p = self(plugin);
	for (const auto &info : kParams)
		p->values[info.id].store(info.defaultValue, std::memory_order_relaxed);
	p->hostFd = static_cast<const clap_host_posix_fd_support *>(
		p->host->get_extension(p->host, CLAP_EXT_POSIX_FD_SUPPORT));
	return true;
}

void guiDestroy(const clap_plugin *plugin);

void pluginDestroy(const clap_plugin *plugin)
{
	guiDestroy(plugin);
	delete self(plugin);
}

bool pluginActivate(const clap_plugin *plugin, double sampleRate, uint32_t, uint32_t)
{
	self(plugin)->sampleRate.store(sampleRate, std::memory_order_relaxed);
	return true;
}

void pluginDeactivate(const clap_plugin *) {}

bool pluginStartProcessing(const clap_plugin *)
{
	return true;
}

void pluginStopProcessing(const clap_plugin *) {}

void pluginReset(const clap_plugin *) {}

clap_process_status pluginProcess(const clap_plugin *plugin, const clap_process *process)
{
	DenormalGuard denormals;
	auto *p = self(plugin);
	applyEvents(p, process->in_events);
	const uint32_t frames = process->frames_count;
	if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1 || frames == 0)
		return CLAP_PROCESS_CONTINUE;
	const clap_audio_buffer &in = process->audio_inputs[0];
	const clap_audio_buffer &out = process->audio_outputs[0];
	if (!in.data32 || !out.data32)
		return CLAP_PROCESS_ERROR;
	// Analyse the input before the output overwrites it when the host processes in place.
	if (in.channel_count >= 2)
		p->ring.writeMid(in.data32[0], in.data32[1], frames);
	else if (in.channel_count == 1)
		p->ring.writeMid(in.data32[0], in.data32[0], frames);
	const uint32_t channels = std::min(in.channel_count, out.channel_count);
	for (uint32_t c = 0; c < channels; ++c)
		if (out.data32[c] != in.data32[c])
			std::memcpy(out.data32[c], in.data32[c], sizeof(float) * frames);
	return CLAP_PROCESS_CONTINUE;
}

// --- audio ports ---

uint32_t portsCount(const clap_plugin *, bool)
{
	return 1;
}

bool portsGet(const clap_plugin *, uint32_t index, bool isInput, clap_audio_port_info *info)
{
	if (index != 0)
		return false;
	info->id = 0;
	std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Input" : "Output");
	info->flags = CLAP_AUDIO_PORT_IS_MAIN;
	info->channel_count = 2;
	info->port_type = CLAP_PORT_STEREO;
	info->in_place_pair = 0;
	return true;
}

const clap_plugin_audio_ports kAudioPorts = {portsCount, portsGet};

// --- params ---

uint32_t paramsCount(const clap_plugin *)
{
	return kParamCount;
}

bool paramsGetInfo(const clap_plugin *, uint32_t index, clap_param_info *info)
{
	if (index >= kParamCount)
		return false;
	const auto &param = kParams[index];
	std::memset(info, 0, sizeof(*info));
	info->id = param.id;
	info->flags = param.flags;
	info->min_value = param.min;
	info->max_value = param.max;
	info->default_value = param.defaultValue;
	std::snprintf(info->name, sizeof(info->name), "%s", param.name);
	return true;
}

bool paramsGetValue(const clap_plugin *plugin, clap_id id, double *value)
{
	if (id >= kParamCount)
		return false;
	*value = self(plugin)->values[id].load(std::memory_order_relaxed);
	return true;
}

const std::array<const char *, 3> *choiceNames(clap_id id)
{
	switch (id) {
	case kResolution:
		return &kResolutionNames;
	case kRange:
		return &kRangeNames;
	case kTilt:
		return &kTiltNames;
	default:
		return nullptr;
	}
}

bool paramsValueToText(const clap_plugin *, clap_id id, double value, char *text, uint32_t size)
{
	if (id >= kParamCount)
		return false;
	if (const auto *names = choiceNames(id))
		std::snprintf(text, size, "%s", (*names)[size_t(std::clamp(int(std::lround(value)), 0, 2))]);
	else
		std::snprintf(text, size, "%.0f %%", value);
	return true;
}

bool paramsTextToValue(const clap_plugin *, clap_id id, const char *text, double *value)
{
	if (id >= kParamCount)
		return false;
	if (const auto *names = choiceNames(id)) {
		for (size_t s = 0; s < names->size(); ++s)
			if (!std::strcmp(text, (*names)[s])) {
				*value = double(s);
				return true;
			}
		return false;
	}
	char *end = nullptr;
	const double parsed = std::strtod(text, &end);
	if (end == text)
		return false;
	*value = clampParam(id, parsed);
	return true;
}

void paramsFlush(const clap_plugin *plugin, const clap_input_events *in, const clap_output_events *)
{
	applyEvents(self(plugin), in);
}

const clap_plugin_params kParamsExt = {paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
				       paramsTextToValue, paramsFlush};

// --- state ---

bool writeAll(const clap_ostream *stream, const void *data, size_t size)
{
	const auto *bytes = static_cast<const uint8_t *>(data);
	while (size > 0) {
		const int64_t written = stream->write(stream, bytes, size);
		if (written <= 0)
			return false;
		bytes += written;
		size -= size_t(written);
	}
	return true;
}

bool readAll(const clap_istream *stream, void *data, size_t size)
{
	auto *bytes = static_cast<uint8_t *>(data);
	while (size > 0) {
		const int64_t read = stream->read(stream, bytes, size);
		if (read <= 0)
			return false;
		bytes += read;
		size -= size_t(read);
	}
	return true;
}

bool stateSave(const clap_plugin *plugin, const clap_ostream *stream)
{
	auto *p = self(plugin);
	const uint32_t header[2] = {kStateMagic, kParamCount};
	if (!writeAll(stream, header, sizeof(header)))
		return false;
	for (const auto &value : p->values) {
		const double v = value.load(std::memory_order_relaxed);
		if (!writeAll(stream, &v, sizeof(v)))
			return false;
	}
	return true;
}

bool stateLoad(const clap_plugin *plugin, const clap_istream *stream)
{
	auto *p = self(plugin);
	uint32_t header[2] = {};
	if (!readAll(stream, header, sizeof(header)) || header[0] != kStateMagic)
		return false;
	for (uint32_t i = 0; i < header[1]; ++i) {
		double v = 0.0;
		if (!readAll(stream, &v, sizeof(v)))
			return false;
		if (i < kParamCount)
			p->values[i].store(clampParam(i, v), std::memory_order_relaxed);
	}
	// The loaded values replace what the host shows, so the host has to read them again.
	const auto *hostParams = static_cast<const clap_host_params *>(p->host->get_extension(p->host, CLAP_EXT_PARAMS));
	if (hostParams && hostParams->rescan)
		hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
	return true;
}

const clap_plugin_state kState = {stateSave, stateLoad};

// --- gui ---

#if defined(_WIN32)
constexpr const char *kWindowApi = CLAP_WINDOW_API_WIN32;
#else
constexpr const char *kWindowApi = CLAP_WINDOW_API_X11;
#endif

bool guiIsApiSupported(const clap_plugin *, const char *api, bool isFloating)
{
	return !isFloating && std::strcmp(api, kWindowApi) == 0;
}

bool guiGetPreferredApi(const clap_plugin *, const char **api, bool *isFloating)
{
	*api = kWindowApi;
	*isFloating = false;
	return true;
}

// Called for every frame the window draws. Reads the samples the audio thread wrote since the previous frame,
// updates the spectrum, and asks for the next frame.
void drawFrame(Plugin *p, visage::Canvas &canvas)
{
	visage::ApplicationWindow *window = p->window.get();
	const ssp::DisplaySettings settings = displaySettings(p);
	p->analyzer.setSampleRate(p->sampleRate.load(std::memory_order_relaxed));
	p->analyzer.setSettings(settings);
	const size_t count = p->ring.read(p->ringCursor, p->scratch.data(), p->scratch.size());
	p->analyzer.push(p->scratch.data(), count);
	p->analyzer.update();
	const int width = int(std::lround(window->width()));
	const int height = int(std::lround(window->height()));
	ssp::drawSpectrum(canvas, width, height, p->analyzer.columns(ssp::plotArea(width, height).width),
			  settings.floorDb);
	window->redraw();
}

bool guiCreate(const clap_plugin *plugin, const char *api, bool isFloating)
{
	auto *p = self(plugin);
	if (!guiIsApiSupported(plugin, api, isFloating))
		return false;
	if (p->window)
		return true;
	p->window = std::make_unique<visage::ApplicationWindow>();
	p->window->setNativeWindowDimensions(kDefaultWidth, kDefaultHeight);
	p->ringCursor = p->ring.written();
	p->window->onDraw() = [p](visage::Canvas &canvas) { drawFrame(p, canvas); };
	return true;
}

void guiDestroy(const clap_plugin *plugin)
{
	auto *p = self(plugin);
	if (!p->window)
		return;
	if (p->registeredFd >= 0 && p->hostFd) {
		p->hostFd->unregister_fd(p->host, p->registeredFd);
		p->registeredFd = -1;
	}
	p->window->close();
	p->window.reset();
}

bool guiSetScale(const clap_plugin *, double)
{
	// Visage reads the scale of the monitor from the window itself.
	return false;
}

bool guiGetSize(const clap_plugin *plugin, uint32_t *width, uint32_t *height)
{
	auto *p = self(plugin);
	if (!p->window)
		return false;
	*width = uint32_t(p->window->nativeWidth());
	*height = uint32_t(p->window->nativeHeight());
	return true;
}

bool guiCanResize(const clap_plugin *)
{
	return true;
}

bool guiGetResizeHints(const clap_plugin *, clap_gui_resize_hints_t *hints)
{
	hints->can_resize_horizontally = true;
	hints->can_resize_vertically = true;
	hints->preserve_aspect_ratio = false;
	hints->aspect_ratio_width = 0;
	hints->aspect_ratio_height = 0;
	return true;
}

bool guiAdjustSize(const clap_plugin *, uint32_t *width, uint32_t *height)
{
	*width = std::max<uint32_t>(*width, kMinWidth);
	*height = std::max<uint32_t>(*height, kMinHeight);
	return true;
}

bool guiSetSize(const clap_plugin *plugin, uint32_t width, uint32_t height)
{
	auto *p = self(plugin);
	if (!p->window)
		return false;
	p->window->setNativeWindowDimensionsFromHost(int(std::max<uint32_t>(width, kMinWidth)),
						     int(std::max<uint32_t>(height, kMinHeight)));
	return true;
}

bool guiSetParent(const clap_plugin *plugin, const clap_window *parent)
{
	auto *p = self(plugin);
	if (!p->window)
		return false;
	p->window->show(parent->ptr);
#if !defined(_WIN32)
	// The host's event loop reads the X11 connection, because a plug-in cannot run its own loop.
	if (p->hostFd && p->window->window() && p->registeredFd < 0) {
		const int fd = p->window->window()->posixFd();
		if (fd >= 0 && p->hostFd->register_fd(p->host, fd, CLAP_POSIX_FD_READ | CLAP_POSIX_FD_ERROR))
			p->registeredFd = fd;
	}
#endif
	return true;
}

bool guiSetTransient(const clap_plugin *, const clap_window *)
{
	return false;
}

void guiSuggestTitle(const clap_plugin *, const char *) {}

bool guiShow(const clap_plugin *plugin)
{
	return self(plugin)->window != nullptr;
}

bool guiHide(const clap_plugin *plugin)
{
	return self(plugin)->window != nullptr;
}

const clap_plugin_gui kGui = {guiIsApiSupported, guiGetPreferredApi, guiCreate,	   guiDestroy,
			      guiSetScale,	 guiGetSize,	     guiCanResize,  guiGetResizeHints,
			      guiAdjustSize,	 guiSetSize,	     guiSetParent,  guiSetTransient,
			      guiSuggestTitle,	 guiShow,	     guiHide};

// --- posix fd ---

void onFd(const clap_plugin *plugin, int, clap_posix_fd_flags_t)
{
	auto *p = self(plugin);
	if (p->window && p->window->window())
		p->window->window()->processPluginFdEvents();
}

const clap_plugin_posix_fd_support kPosixFd = {onFd};

const void *pluginGetExtension(const clap_plugin *, const char *id)
{
	if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
		return &kAudioPorts;
	if (!std::strcmp(id, CLAP_EXT_PARAMS))
		return &kParamsExt;
	if (!std::strcmp(id, CLAP_EXT_STATE))
		return &kState;
	if (!std::strcmp(id, CLAP_EXT_GUI))
		return &kGui;
#if !defined(_WIN32)
	if (!std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT))
		return &kPosixFd;
#endif
	return nullptr;
}

void pluginOnMainThread(const clap_plugin *) {}

const clap_plugin *createPlugin(const clap_host *host)
{
	auto *p = new Plugin();
	p->host = host;
	p->plugin.desc = &kDescriptor;
	p->plugin.plugin_data = p;
	p->plugin.init = pluginInit;
	p->plugin.destroy = pluginDestroy;
	p->plugin.activate = pluginActivate;
	p->plugin.deactivate = pluginDeactivate;
	p->plugin.start_processing = pluginStartProcessing;
	p->plugin.stop_processing = pluginStopProcessing;
	p->plugin.reset = pluginReset;
	p->plugin.process = pluginProcess;
	p->plugin.get_extension = pluginGetExtension;
	p->plugin.on_main_thread = pluginOnMainThread;
	return &p->plugin;
}

// --- factory ---

uint32_t factoryCount(const clap_plugin_factory *)
{
	return 1;
}

const clap_plugin_descriptor *factoryDescriptor(const clap_plugin_factory *, uint32_t index)
{
	return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin *factoryCreate(const clap_plugin_factory *, const clap_host *host, const char *pluginId)
{
	if (!clap_version_is_compatible(host->clap_version) || std::strcmp(pluginId, kDescriptor.id) != 0)
		return nullptr;
	return createPlugin(host);
}

const clap_plugin_factory kFactory = {factoryCount, factoryDescriptor, factoryCreate};

// Without this, clap-wrapper fills the VST 3 factory's e-mail contact with the placeholder "info@".
// The other members stay nullptr, so the VST 3 class ID is the one clap-wrapper derives from the CLAP ID.
const clap_plugin_factory_as_vst3 kVst3FactoryInfo = {nullptr, kDescriptor.url, "", nullptr, nullptr};

} // namespace

bool ssp_entry_init(const char *)
{
	return true;
}

void ssp_entry_deinit(void) {}

const void *ssp_entry_get_factory(const char *factoryId)
{
	if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
		return &kFactory;
	if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_INFO_VST3) == 0 ||
	    std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_INFO_VST3_V1) == 0)
		return &kVst3FactoryInfo;
	return nullptr;
}
