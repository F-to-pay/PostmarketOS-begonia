// SPDX-License-Identifier: GPL-2.0
// mic-nc-dsp: dual-microphone noise suppression (Redmi Note 8 Pro / begonia).
//   stdin : s16le, 48 kHz, stereo: L = bottom (main) microphone AIN0, R = top (noise-cancelling) AIN2
//   stdout: s16le, 48 kHz, MONO: speech with the noise subtracted
//
// Idea: speech is louder at the bottom microphone than at the top one (the mouth is closer), while the background noise is about the same in both
// with a known (calibrated) per-frequency ratio c_k. Noise estimate in the bottom channel: N_k = c_k * P2_k
// (P2 - the power spectrum of the top channel). Then spectral subtraction with a minimum gain
// GMIN: G_k = clamp(1 - ALPHA * N_k / P1_k, GMIN, 1), smoothed over time and frequency.
// STFT: sqrt-Hann window 512, hop 256, overlap-add.
//
// Modes:   mic-nc-dsp --calibrate file.cal    (the recording contains ONLY room noise, no speech: computes c_k)
//          mic-nc-dsp [--cal file.cal]        (working mode)
// Environment variables: NC_ALPHA (default 1.4), NC_GMIN (0.12), NC_GAIN_DB (output gain, +18).
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#define N 512
#define H 256
#define NB (N / 2 + 1)

static void fft(float *re, float *im, int n, int inv)
{
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			float t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (int len = 2; len <= n; len <<= 1) {
		float ang = 2.0f * (float)M_PI / len * (inv ? 1 : -1);
		float wr = cosf(ang), wi = sinf(ang);
		for (int i = 0; i < n; i += len) {
			float cr = 1, ci = 0;
			for (int j = 0; j < len / 2; j++) {
				int a = i + j, b = i + j + len / 2;
				float ur = re[a], ui = im[a];
				float vr = re[b] * cr - im[b] * ci;
				float vi = re[b] * ci + im[b] * cr;
				re[a] = ur + vr; im[a] = ui + vi;
				re[b] = ur - vr; im[b] = ui - vi;
				float ncr = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = ncr;
			}
		}
	}
	if (inv)
		for (int i = 0; i < n; i++) {
			re[i] /= n;
			im[i] /= n;
		}
}

int main(int argc, char **argv)
{
	const char *calfile = NULL, *calout = NULL;
	float alpha = getenv("NC_ALPHA") ? atof(getenv("NC_ALPHA")) : 1.4f;
	float gmin = getenv("NC_GMIN") ? atof(getenv("NC_GMIN")) : 0.12f;
	/* NC_GAIN_DB: output gain in dB (default +18: the bottom microphone records speech quietly, -56 dBFS) */
	float gain = powf(10.0f, (getenv("NC_GAIN_DB") ? atof(getenv("NC_GAIN_DB")) : 18.0f) / 20.0f);
	static float win[N], c[NB], S1[NB], S2[NB], Gs[NB], acc1[NB], acc2[NB];
	static float b1[N], b2[N], r1[N], i1[N], r2[N], i2[N], olap[N];
	int16_t in[2 * H], out[H];

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--calibrate") && i + 1 < argc)
			calout = argv[++i];
		else if (!strcmp(argv[i], "--cal") && i + 1 < argc)
			calfile = argv[++i];
	}
	for (int i = 0; i < N; i++)
		win[i] = sqrtf(0.5f * (1.0f - cosf(2.0f * (float)M_PI * (i + 0.5f) / N)));
	for (int k = 0; k < NB; k++) {
		c[k] = 1.0f;
		Gs[k] = 1.0f;
	}
	if (calfile) {
		FILE *f = fopen(calfile, "r");
		if (f) {
			for (int k = 0; k < NB; k++)
				if (fscanf(f, "%f", &c[k]) != 1) { c[k] = 1.0f; }
			fclose(f);
			fprintf(stderr, "mic-nc-dsp: calibration loaded (%s)\n", calfile);
		} else {
			fprintf(stderr, "mic-nc-dsp: no calibration file %s, c_k = 1\n", calfile);
		}
	}

	long frames = 0;
	int live = getenv("NC_LIVE") != NULL;
	int dbg = getenv("NC_LOG") != NULL;
	if (live) {
#ifdef F_SETPIPE_SZ
		fcntl(STDOUT_FILENO, F_SETPIPE_SZ, 4096);
#endif
		fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK);
	}
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (fread(in, sizeof(int16_t), 2 * H, stdin) == 2 * H) {
		memmove(b1, b1 + H, (N - H) * sizeof(float));
		memmove(b2, b2 + H, (N - H) * sizeof(float));
		for (int i = 0; i < H; i++) {
			b1[N - H + i] = in[2 * i] / 32768.0f;
			b2[N - H + i] = in[2 * i + 1] / 32768.0f;
		}
		for (int i = 0; i < N; i++) {
			r1[i] = b1[i] * win[i]; i1[i] = 0;
			r2[i] = b2[i] * win[i]; i2[i] = 0;
		}
		fft(r1, i1, N, 0);
		fft(r2, i2, N, 0);

		for (int k = 0; k < NB; k++) {
			float p1 = r1[k] * r1[k] + i1[k] * i1[k];
			float p2 = r2[k] * r2[k] + i2[k] * i2[k];
			S1[k] = 0.6f * S1[k] + 0.4f * p1;
			S2[k] = 0.6f * S2[k] + 0.4f * p2;
			if (calout && frames > 100) {	/* the first ~0.5 s - microphone warm-up, not counted */
				acc1[k] += p1;
				acc2[k] += p2;
			}
		}
		frames++;
		if (dbg && frames % 188 == 0) {	/* ~1 s of audio (188 * 256 / 48000) */
			struct timespec t1;
			clock_gettime(CLOCK_MONOTONIC, &t1);
			double wall = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
			fprintf(stderr, "mic-nc-dsp: wall %.2f s, audio processed %.2f s, difference %+.2f s\n", wall, frames * (double)H / 48000.0, frames * (double)H / 48000.0 - wall);
		}
		if (calout)
			continue;

		float g[NB];
		for (int k = 0; k < NB; k++) {
			float nn = c[k] * S2[k];
			float t = 1.0f - alpha * nn / (S1[k] + 1e-12f);
			if (t < gmin) t = gmin;
			if (t > 1.0f) t = 1.0f;
			/* open fast (speech), close a bit slower (less "musical noise") */
			Gs[k] = (t > Gs[k]) ? 0.4f * Gs[k] + 0.6f * t : 0.75f * Gs[k] + 0.25f * t;
		}
		for (int k = 0; k < NB; k++) {
			float a = Gs[k > 0 ? k - 1 : 0], b = Gs[k], d = Gs[k < NB - 1 ? k + 1 : NB - 1];
			g[k] = 0.25f * a + 0.5f * b + 0.25f * d;
		}
		for (int k = 0; k < NB; k++) {
			r1[k] *= g[k]; i1[k] *= g[k];
			if (k > 0 && k < NB - 1) {
				r1[N - k] *= g[k]; i1[N - k] *= g[k];
			}
		}
		fft(r1, i1, N, 1);
		for (int i = 0; i < N; i++)
			olap[i] += r1[i] * win[i];
		for (int i = 0; i < H; i++) {
			/* gain compensation (after noise suppression) + soft limiter: linear up to 0.7, then smoothly towards 1.0 */
			float x = olap[i] * gain;
			float ax = x < 0 ? -x : x;
			if (ax > 0.7f)
				ax = 0.7f + 0.3f * tanhf((ax - 0.7f) / 0.3f);
			float v = (x < 0 ? -ax : ax) * 32767.0f;
			out[i] = (int16_t)v;
		}
		memmove(olap, olap + H, (N - H) * sizeof(float));
		memset(olap + (N - H), 0, H * sizeof(float));
		if (live) {
			/* live mode: the pipe is non-blocking and tiny; if there is no reader/it cannot keep up - the frame is dropped,
			 * not accumulated (otherwise the reader gets stale data: PulseAudio does not read the FIFO while the source is idle) */
			const char *p = (const char *)out;
			size_t left = H * sizeof(int16_t);
			while (left) {
				ssize_t w = write(STDOUT_FILENO, p, left);
				if (w > 0) {
					p += w;
					left -= w;
				} else {
					break;	/* EAGAIN etc.: drop the rest */
				}
			}
		} else {
			fwrite(out, sizeof(int16_t), H, stdout);
			fflush(stdout);
		}
	}

	if (calout) {
		FILE *f = fopen(calout, "w");
		if (!f) {
			perror("calibrate: fopen");
			return 1;
		}
		float raw[NB];
		for (int k = 0; k < NB; k++)
			raw[k] = acc1[k] / (acc2[k] + 1e-20f);
		for (int k = 0; k < NB; k++) {
			float s = 0; int n = 0;
			for (int j = k - 4; j <= k + 4; j++)
				if (j >= 0 && j < NB) { s += raw[j]; n++; }
			float v = s / n;
			if (v < 0.05f) v = 0.05f;
			if (v > 20.0f) v = 20.0f;
			fprintf(f, "%.5f\n", v);
		}
		fclose(f);
		fprintf(stderr, "mic-nc-dsp: calibration written to %s (frames %ld)\n", calout, frames);
	}
	return 0;
}
