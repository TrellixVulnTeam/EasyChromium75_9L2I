/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "vcs_version.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_IO_H
# include <io.h>
#endif
#ifdef _WIN32
# include <windows.h>
#endif

#include "dav1d/dav1d.h"

#include "input/input.h"

#include "output/output.h"

#include "dav1d_cli_parse.h"

static uint64_t get_time_nanos(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return 1000000000 * t.QuadPart / frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return 1000000000ULL * ts.tv_sec + ts.tv_nsec;
#endif
}

static void sleep_nanos(uint64_t d) {
#ifdef _WIN32
    Sleep((unsigned)(d / 1000000));
#else
    const struct timespec ts = {
        .tv_sec = (time_t)(d / 1000000000),
        .tv_nsec = d % 1000000000,
    };
    nanosleep(&ts, NULL);
#endif
}

static void synchronize(const int realtime, const unsigned cache,
                        const unsigned n_out, const uint64_t nspf,
                        const uint64_t tfirst, uint64_t *const elapsed,
                        FILE *const frametimes)
{
    const uint64_t tcurr = get_time_nanos();
    const uint64_t last = *elapsed;
    *elapsed = tcurr - tfirst;
    if (realtime) {
        const uint64_t deadline = nspf * n_out;
        if (*elapsed < deadline) {
            const uint64_t remaining = deadline - *elapsed;
            if (remaining > nspf * cache) sleep_nanos(remaining - nspf * cache);
            *elapsed = deadline;
        }
    }
    if (frametimes) {
        const uint64_t frametime = *elapsed - last;
        fprintf(frametimes, "%" PRIu64 "\n", frametime);
        fflush(frametimes);
    }
}

static void print_stats(const int istty, const unsigned n, const unsigned num,
                        const uint64_t elapsed, const double i_fps)
{
    if (istty) fputs("\r", stderr);
    const double d_fps = 1e9 * n / elapsed;
    const double speed = d_fps / i_fps;
    if (num == 0xFFFFFFFF) {
        fprintf(stderr, "Decoded %u frames", n);
    } else {
        fprintf(stderr, "Decoded %u/%u frames (%.1lf%%)", n, num,
                100.0 * n / num);
    }
    if (i_fps)
        fprintf(stderr, " - %.2lf/%.2lf fps (%.2lfx)", d_fps, i_fps, speed);
    if (!istty) fputs("\n", stderr);
}

int main(const int argc, char *const *const argv) {
    const int istty = isatty(fileno(stderr));
    int res = 0;
    CLISettings cli_settings;
    Dav1dSettings lib_settings;
    DemuxerContext *in;
    MuxerContext *out = NULL;
    Dav1dPicture p;
    Dav1dContext *c;
    Dav1dData data;
    unsigned n_out = 0, total, fps[2];
    uint64_t nspf, tfirst, elapsed;
    double i_fps;
    FILE *frametimes = NULL;
    const char *version = dav1d_version();

    if (strcmp(version, DAV1D_VERSION)) {
        fprintf(stderr, "Version mismatch (library: %s, executable: %s)\n",
                version, DAV1D_VERSION);
        return -1;
    }

    init_demuxers();
    init_muxers();
    parse(argc, argv, &cli_settings, &lib_settings);

    if ((res = input_open(&in, cli_settings.demuxer,
                          cli_settings.inputfile,
                          fps, &total)) < 0)
    {
        return res;
    }
    for (unsigned i = 0; i <= cli_settings.skip; i++) {
        if ((res = input_read(in, &data)) < 0) {
            input_close(in);
            return res;
        }
        if (i < cli_settings.skip) dav1d_data_unref(&data);
    }

    if (!cli_settings.quiet)
        fprintf(stderr, "dav1d %s - by VideoLAN\n", dav1d_version());

    // skip frames until a sequence header is found
    if (cli_settings.skip) {
        Dav1dSequenceHeader seq;
        unsigned seq_skip = 0;
        while (dav1d_parse_sequence_header(&seq, data.data, data.sz)) {
            if ((res = input_read(in, &data)) < 0) {
                input_close(in);
                return res;
            }
            seq_skip++;
        }
        if (seq_skip && !cli_settings.quiet)
            fprintf(stderr,
                    "skipped %u packets due to missing sequence header\n",
                    seq_skip);
    }

    //getc(stdin);
    if (cli_settings.limit != 0 && cli_settings.limit < total)
        total = cli_settings.limit;

    if ((res = dav1d_open(&c, &lib_settings)))
        return res;

    if (cli_settings.frametimes)
        frametimes = fopen(cli_settings.frametimes, "w");

    if (cli_settings.realtime != REALTIME_CUSTOM) {
        if (fps[1] == 0) {
            i_fps = 0;
            nspf = 0;
        } else {
            i_fps = (double)fps[0] / fps[1];
            nspf = 1000000000ULL * fps[1] / fps[0];
        }
    } else {
        i_fps = cli_settings.realtime_fps;
        nspf = 1000000000.0 / cli_settings.realtime_fps;
    }
    tfirst = get_time_nanos();

    do {
        memset(&p, 0, sizeof(p));
        if ((res = dav1d_send_data(c, &data)) < 0) {
            if (res != DAV1D_ERR(EAGAIN)) {
                fprintf(stderr, "Error decoding frame: %s\n",
                        strerror(-res));
                break;
            }
        }

        if ((res = dav1d_get_picture(c, &p)) < 0) {
            if (res != DAV1D_ERR(EAGAIN)) {
                fprintf(stderr, "Error decoding frame: %s\n",
                        strerror(-res));
                break;
            }
            res = 0;
        } else {
            if (!n_out) {
                if ((res = output_open(&out, cli_settings.muxer,
                                       cli_settings.outputfile,
                                       &p.p, fps)) < 0)
                {
                    if (frametimes) fclose(frametimes);
                    return res;
                }
            }
            if ((res = output_write(out, &p)) < 0)
                break;
            n_out++;
            if (nspf) {
                synchronize(cli_settings.realtime, cli_settings.realtime_cache,
                            n_out, nspf, tfirst, &elapsed, frametimes);
            }
            if (!cli_settings.quiet)
                print_stats(istty, n_out, total, elapsed, i_fps);
        }

        if (cli_settings.limit && n_out == cli_settings.limit)
            break;
    } while (data.sz > 0 || !input_read(in, &data));

    if (data.sz > 0) dav1d_data_unref(&data);

    // flush
    if (res == 0) while (!cli_settings.limit || n_out < cli_settings.limit) {
        if ((res = dav1d_get_picture(c, &p)) < 0) {
            if (res != DAV1D_ERR(EAGAIN)) {
                fprintf(stderr, "Error decoding frame: %s\n",
                        strerror(-res));
            } else {
                res = 0;
                break;
            }
        } else {
            if (!n_out) {
                if ((res = output_open(&out, cli_settings.muxer,
                                       cli_settings.outputfile,
                                       &p.p, fps)) < 0)
                {
                    if (frametimes) fclose(frametimes);
                    return res;
                }
            }
            if ((res = output_write(out, &p)) < 0)
                break;
            n_out++;
            if (nspf) {
                synchronize(cli_settings.realtime, cli_settings.realtime_cache,
                            n_out, nspf, tfirst, &elapsed, frametimes);
            }
            if (!cli_settings.quiet)
                print_stats(istty, n_out, total, elapsed, i_fps);
        }
    }

    if (frametimes) fclose(frametimes);

    input_close(in);
    if (out) {
        if (!cli_settings.quiet && istty)
            fprintf(stderr, "\n");
        if (cli_settings.verify)
            res |= output_verify(out, cli_settings.verify);
        else
            output_close(out);
    } else {
        fprintf(stderr, "No data decoded\n");
        res = 1;
    }
    dav1d_close(&c);

    return res;
}
