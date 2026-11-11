/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.9: the startup video (ogbox_intro.h). libvpx (BSD-3-Clause, the ports release's,
 * ps5/tools/build-deps.sh) decodes the VP9 frames; they're turned into RGBA here.
 */
#include "ogbox_intro.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include "platform.h"
#include "platform/ps5_bootlog.h"

namespace ogbox {

namespace {

uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) {
		return false;
	}
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	out.resize(size > 0 ? static_cast<size_t>(size) : 0);
	const bool ok = size > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
	std::fclose(f);
	return ok;
}

inline uint8_t clamp8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

/* I420 (BT.601, studio range: what the encoder was given) into RGBA, rows y0 to y1 */
void toRgba(const vpx_image_t *img, uint8_t *out, uint32_t w, uint32_t y0, uint32_t y1)
{
	for (uint32_t y = y0; y < y1; y++) {
		const uint8_t *py = img->planes[VPX_PLANE_Y] + static_cast<size_t>(y) * img->stride[VPX_PLANE_Y];
		const uint8_t *pu = img->planes[VPX_PLANE_U] + static_cast<size_t>(y >> 1) * img->stride[VPX_PLANE_U];
		const uint8_t *pv = img->planes[VPX_PLANE_V] + static_cast<size_t>(y >> 1) * img->stride[VPX_PLANE_V];
		uint8_t *o = out + static_cast<size_t>(y) * w * 4;
		for (uint32_t x = 0; x < w; x += 2) {
			const int u = pu[x >> 1] - 128, v = pv[x >> 1] - 128;
			const int r = 409 * v + 128, g = -100 * u - 208 * v + 128, b = 516 * u + 128;
			for (uint32_t k = 0; k < 2 && x + k < w; k++) {
				const int c = 298 * (py[x + k] - 16);
				o[0] = clamp8((c + r) >> 8);
				o[1] = clamp8((c + g) >> 8);
				o[2] = clamp8((c + b) >> 8);
				o[3] = 255;
				o += 4;
			}
		}
	}
}

constexpr int kSlots = 12;          // decoded ahead: a fifth of a second at 60 frames a second
constexpr int kPreroll = 8;         // decoded before the clock starts (start() waits for them)
constexpr int kConverters = 4;      // threads that turn a frame into RGBA, a stripe each
constexpr double kFade = 0.2;       // seconds: the sound's fade when skipped

} // namespace

struct IntroPlayer::Impl {
	std::vector<uint8_t> file;           // the IVF
	struct Packet {
		size_t offset;
		uint32_t size;
		double time;                     // seconds
	};
	std::vector<Packet> packets;
	uint32_t width = 0, height = 0;
	double length = 0.0;

	struct Slot {
		std::vector<uint8_t> rgba;
		int index = -1;                  // the frame it holds; -1: free
	};
	Slot slots[kSlots];
	int shown = -1;                      // the slot frame() last gave
	std::mutex mu;
	std::condition_variable cv;
	std::thread decoder;
	std::atomic<bool> quit{ false };
	std::atomic<int> wanted{ 0 };        // the frame the clock is at: frames before it aren't converted
	std::atomic<int> decoded{ -1 };      // the newest frame decoded and converted
	std::atomic<bool> done{ false };     // the decoder is through
	uint64_t serial = 0;

	/* the conversion's helpers: each frame, stripes 1..kConverters-1 (the decoder does stripe 0) */
	std::thread converters[kConverters - 1];
	std::mutex cmu;
	std::condition_variable cgo, cdone;
	const vpx_image_t *cimg = nullptr;
	uint8_t *cout = nullptr;
	uint64_t cjob = 0;
	int cleft = 0;
	bool cquit = false;

	void converter(int stripe)
	{
		uint64_t seen = 0;
		for (;;) {
			std::unique_lock<std::mutex> lock(cmu);
			cgo.wait(lock, [&] { return cquit || cjob != seen; });
			if (cquit) {
				return;
			}
			seen = cjob;
			const vpx_image_t *img = cimg;
			uint8_t *out = cout;
			lock.unlock();
			convertStripe(img, out, stripe);
			lock.lock();
			if (--cleft == 0) {
				cdone.notify_one();
			}
		}
	}

	void convertStripe(const vpx_image_t *img, uint8_t *out, int stripe)
	{
		const uint32_t h = std::min<uint32_t>(img->d_h, height), w = std::min<uint32_t>(img->d_w, width);
		const uint32_t rows = ((h + kConverters - 1) / kConverters + 1) & ~1u;   // even: a chroma row is two
		const uint32_t y0 = std::min(h, rows * static_cast<uint32_t>(stripe)), y1 = std::min(h, y0 + rows);
		toRgba(img, out, w, y0, y1);
	}

	void convert(const vpx_image_t *img, uint8_t *out)
	{
		{
			std::lock_guard<std::mutex> lock(cmu);
			cimg = img;
			cout = out;
			cleft = kConverters - 1;
			cjob++;
		}
		cgo.notify_all();
		convertStripe(img, out, 0);
		std::unique_lock<std::mutex> lock(cmu);
		cdone.wait(lock, [&] { return cleft == 0; });
	}

	std::vector<int16_t> pcm;            // interleaved stereo, 48 kHz
	std::atomic<size_t> pos{ 0 };        // in frames
	std::atomic<bool> fading{ false };
	float gain = 1.0f;
	bool sound = false;

	double startedAt = -1.0;
	double skippedAt = -1.0;

	static void fill(int16_t *out, int count, void *user)
	{
		Impl *self = static_cast<Impl *>(user);
		const size_t total = self->pcm.size() / 2;
		size_t p = self->pos.load(std::memory_order_relaxed);
		const float step = 1.0f / static_cast<float>(kFade * 48000.0);
		for (int i = 0; i < count; i++, p++) {
			if (self->fading.load(std::memory_order_relaxed)) {
				self->gain = self->gain > step ? self->gain - step : 0.0f;
			}
			if (p < total && self->gain > 0.0f) {
				out[2 * i] = static_cast<int16_t>(self->pcm[2 * p] * self->gain);
				out[2 * i + 1] = static_cast<int16_t>(self->pcm[2 * p + 1] * self->gain);
			} else {
				out[2 * i] = out[2 * i + 1] = 0;
			}
		}
		self->pos.store(p, std::memory_order_relaxed);
	}

	bool parseIvf()
	{
		if (file.size() < 32 || std::memcmp(file.data(), "DKIF", 4) != 0 || std::memcmp(file.data() + 8, "VP90", 4) != 0) {
			return false;
		}
		const uint32_t header = rd16(file.data() + 6);
		width = rd16(file.data() + 12);
		height = rd16(file.data() + 14);
		const uint32_t den = rd32(file.data() + 16), num = rd32(file.data() + 20);
		if (!width || !height || !den || !num) {
			return false;
		}
		for (size_t at = header; at + 12 <= file.size();) {
			const uint32_t size = rd32(file.data() + at);
			const uint64_t pts = rd32(file.data() + at + 4) | static_cast<uint64_t>(rd32(file.data() + at + 8)) << 32;
			if (at + 12 + size > file.size()) {
				break;
			}
			packets.push_back({ at + 12, size, static_cast<double>(pts) * num / den });
			at += 12 + size;
		}
		if (packets.empty()) {
			return false;
		}
		length = packets.back().time + (packets.size() > 1 ? packets.back().time - packets[packets.size() - 2].time : 0.0);
		return true;
	}

	bool parseWav(const std::vector<uint8_t> &w)
	{
		if (w.size() < 12 || std::memcmp(w.data(), "RIFF", 4) != 0 || std::memcmp(w.data() + 8, "WAVE", 4) != 0) {
			return false;
		}
		bool fmtOk = false;
		for (size_t at = 12; at + 8 <= w.size();) {
			const uint32_t size = rd32(w.data() + at + 4);
			const uint8_t *body = w.data() + at + 8;
			if (at + 8 + size > w.size()) {
				break;
			}
			if (!std::memcmp(w.data() + at, "fmt ", 4) && size >= 16) {
				fmtOk = rd16(body) == 1 && rd16(body + 2) == 2 && rd32(body + 4) == 48000 && rd16(body + 14) == 16;
			} else if (!std::memcmp(w.data() + at, "data", 4) && fmtOk) {
				pcm.resize(size / 2);
				std::memcpy(pcm.data(), body, pcm.size() * 2);
				return true;
			}
			at += 8 + size + (size & 1);
		}
		return false;
	}

	void decode()
	{
		vpx_codec_ctx_t codec;
		vpx_codec_dec_cfg_t cfg{};
		cfg.threads = 6;
		cfg.w = width;
		cfg.h = height;
		if (vpx_codec_dec_init(&codec, vpx_codec_vp9_dx(), &cfg, 0) != VPX_CODEC_OK) {
			pshakux_log("intro: the VP9 decoder cannot start: no video");
			done.store(true);
			return;
		}
		/* row-based threads (within a tile column too) and the faster loop filter */
		vpx_codec_control(&codec, VP9D_SET_ROW_MT, 1);
		vpx_codec_control(&codec, VP9D_SET_LOOP_FILTER_OPT, 1);
		for (int c = 1; c < kConverters; c++) {
			converters[c - 1] = std::thread([this, c] { converter(c); });
		}
		unsigned late = 0, frames = 0;
		double decodeTime = 0.0, convertTime = 0.0, slowest = 0.0;
		for (size_t i = 0; i < packets.size() && !quit.load(); i++) {
			const double t0 = now_seconds();
			if (vpx_codec_decode(&codec, file.data() + packets[i].offset, packets[i].size, nullptr, 0) != VPX_CODEC_OK) {
				pshakux_log("intro: frame %zu cannot be decoded (%s): the video ends there", i, vpx_codec_error(&codec));
				break;
			}
			vpx_codec_iter_t iter = nullptr;
			const vpx_image_t *img = vpx_codec_get_frame(&codec, &iter);
			if (!img || img->fmt != VPX_IMG_FMT_I420) {
				continue;
			}
			const double t1 = now_seconds();
			decodeTime += t1 - t0;
			if (static_cast<int>(i) < wanted.load() && i + 1 < packets.size()) {
				late++;                      // behind the clock: decoded (the next needs it), not shown
				continue;
			}
			std::unique_lock<std::mutex> lock(mu);
			Slot *free = nullptr;
			cv.wait(lock, [&] {
				for (int s = 0; s < kSlots; s++) {
					if (slots[s].index < 0) {
						free = &slots[s];
						return true;
					}
				}
				return quit.load();
			});
			if (quit.load() || !free) {
				break;
			}
			lock.unlock();
			free->rgba.resize(static_cast<size_t>(width) * height * 4);
			const double t2 = now_seconds();
			convert(img, free->rgba.data());
			const double t3 = now_seconds();
			convertTime += t3 - t2;
			slowest = std::max(slowest, (t1 - t0) + (t3 - t2));
			frames++;
			lock.lock();
			free->index = static_cast<int>(i);
			decoded.store(static_cast<int>(i));
		}
		{
			std::lock_guard<std::mutex> lock(cmu);
			cquit = true;
		}
		cgo.notify_all();
		for (std::thread &t : converters) {
			if (t.joinable()) {
				t.join();
			}
		}
		vpx_codec_destroy(&codec);
		done.store(true);
		pshakux_log("intro: decoding %.1f ms a frame, turning into RGBA %.1f ms (%d threads), the slowest frame %.1f ms; "
			"%u frames shown of %zu, %u decoded behind the clock (not shown)", 1000.0 * decodeTime / std::max<size_t>(1, packets.size()),
			1000.0 * convertTime / std::max(1u, frames), kConverters, 1000.0 * slowest, frames, packets.size(), late);
	}
};

IntroPlayer::IntroPlayer() : d(new Impl) {}

IntroPlayer::~IntroPlayer() { close(); }

bool IntroPlayer::open(const std::string &video, const std::string &sound)
{
	if (!readFile(video, d->file) || !d->parseIvf()) {
		pshakux_log("intro: no video (%s %s): the logo instead", video.c_str(), d->file.empty() ? "missing" : "unreadable");
		d->file.clear();
		return false;
	}
	std::vector<uint8_t> wav;
	d->sound = readFile(sound, wav) && d->parseWav(wav);
	pshakux_log("intro: %ux%u, %zu frames, %.2f s%s", d->width, d->height, d->packets.size(), d->length,
		d->sound ? ", with its sound" : ", no sound");
	d->decoder = std::thread([this] { d->decode(); });
	return true;
}

bool IntroPlayer::ready() const
{
	return d->file.empty() || d->done.load() || d->decoded.load() >= std::min<int>(kPreroll, static_cast<int>(d->packets.size())) - 1;
}

void IntroPlayer::start()
{
	if (d->startedAt >= 0.0 || d->file.empty()) {
		return;
	}
	d->startedAt = now_seconds();
	if (d->sound && !audio_start(&Impl::fill, d.get())) {
		pshakux_log("intro: no sound output: the video plays without it");
		d->sound = false;
	}
}

double IntroPlayer::elapsed() const { return d->startedAt < 0.0 ? 0.0 : now_seconds() - d->startedAt; }

double IntroPlayer::duration() const { return d->length; }

bool IntroPlayer::finished() const
{
	if (d->file.empty()) {
		return true;
	}
	if (d->skippedAt >= 0.0) {
		return now_seconds() - d->skippedAt >= kFade;
	}
	return elapsed() >= d->length;
}

void IntroPlayer::skip()
{
	if (d->skippedAt < 0.0) {
		d->skippedAt = now_seconds();
		d->fading.store(true);
	}
}

const uint8_t *IntroPlayer::frame(uint32_t *w, uint32_t *h, uint64_t *serial)
{
	if (d->file.empty()) {
		return nullptr;
	}
	const double t = elapsed();
	int target = 0;
	while (target + 1 < static_cast<int>(d->packets.size()) && d->packets[target + 1].time <= t) {
		target++;
	}
	d->wanted.store(target);
	std::lock_guard<std::mutex> lock(d->mu);
	int best = -1;
	for (int s = 0; s < kSlots; s++) {
		if (d->slots[s].index >= 0 && d->slots[s].index <= target && (best < 0 || d->slots[s].index > d->slots[best].index)) {
			best = s;
		}
	}
	if (best < 0) {
		best = d->shown;                 // none at the clock yet: the last one stays
	}
	bool freed = false;
	for (int s = 0; s < kSlots; s++) {   // what's before the one shown is done with
		if (s != best && d->slots[s].index >= 0 && (best < 0 || d->slots[s].index < d->slots[best].index)) {
			d->slots[s].index = -1;
			freed = true;
		}
	}
	if (freed) {
		d->cv.notify_all();
	}
	if (best < 0 || d->slots[best].index < 0) {
		return nullptr;
	}
	if (best != d->shown) {
		d->shown = best;
		d->serial++;
	}
	*w = d->width;
	*h = d->height;
	*serial = d->serial;
	return d->slots[best].rgba.data();
}

void IntroPlayer::close()
{
	if (!d) {
		return;
	}
	d->quit.store(true);
	d->cv.notify_all();
	if (d->decoder.joinable()) {
		d->decoder.join();
	}
	if (d->sound && d->startedAt >= 0.0) {
		audio_stop();
		d->sound = false;
	}
}

} // namespace ogbox
