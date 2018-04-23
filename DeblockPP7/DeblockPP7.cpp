/*
 * VapourSynth port by HolyWu
 *
 * Copyright (C) 2005 Michael Niedermayer <michaelni@gmx.at>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include <VapourSynth.h>
#include <VSHelper.h>

static constexpr int N = 1 << 16;
static constexpr int N0 = 4;
static constexpr int N1 = 5;
static constexpr int N2 = 10;
static constexpr int SN0 = 2;
static constexpr double SN2 = 3.1622776601683795;

struct DeblockPP7Data {
    VSNodeRef * node;
    const VSVideoInfo * vi;
    bool process[3];
    unsigned thresh[16];
    std::unordered_map<std::thread::id, uint16_t *> bufferI;
    std::unordered_map<std::thread::id, float *> bufferF;
    const int16_t factor[16] = {
        N / (N0 * N0), N / (N0 * N1), N / (N0 * N0), N / (N0 * N2),
        N / (N1 * N0), N / (N1 * N1), N / (N1 * N0), N / (N1 * N2),
        N / (N0 * N0), N / (N0 * N1), N / (N0 * N0), N / (N0 * N2),
        N / (N2 * N0), N / (N2 * N1), N / (N2 * N0), N / (N2 * N2)
    };
};

template<typename T1, typename T2, int scale>
static inline void dctA(const T1 * srcp, T2 * VS_RESTRICT dstp, const int stride) noexcept {
    for (int i = 0; i < 4; i++) {
        T2 s0 = (srcp[0 * stride] + srcp[6 * stride]) * scale;
        T2 s1 = (srcp[1 * stride] + srcp[5 * stride]) * scale;
        T2 s2 = (srcp[2 * stride] + srcp[4 * stride]) * scale;
        T2 s3 = srcp[3 * stride] * scale;
        T2 s = s3 + s3;
        s3 = s - s0;
        s0 = s + s0;
        s = s2 + s1;
        s2 = s2 - s1;
        dstp[0] = s0 + s;
        dstp[2] = s0 - s;
        dstp[1] = 2 * s3 + s2;
        dstp[3] = s3 - 2 * s2;

        srcp++;
        dstp += 4;
    }
}

template<typename T>
static inline void dctB(const T * srcp, T * VS_RESTRICT dstp) noexcept {
    for (int i = 0; i < 4; i++) {
        T s0 = srcp[0 * 4] + srcp[6 * 4];
        T s1 = srcp[1 * 4] + srcp[5 * 4];
        T s2 = srcp[2 * 4] + srcp[4 * 4];
        T s3 = srcp[3 * 4];
        T s = s3 + s3;
        s3 = s - s0;
        s0 = s + s0;
        s = s2 + s1;
        s2 = s2 - s1;
        dstp[0 * 4] = s0 + s;
        dstp[2 * 4] = s0 - s;
        dstp[1 * 4] = 2 * s3 + s2;
        dstp[3 * 4] = s3 - 2 * s2;

        srcp++;
        dstp++;
    }
}

template<typename T>
static void pp7Filter(const VSFrameRef * src, VSFrameRef * dst, const DeblockPP7Data * const VS_RESTRICT d, const VSAPI * vsapi) noexcept {
    const auto threadId = std::this_thread::get_id();
    uint16_t * buffer = d->bufferI.at(threadId);

    for (int plane = 0; plane < d->vi->format->numPlanes; plane++) {
        if (d->process[plane]) {
            const int width = vsapi->getFrameWidth(src, plane);
            const int height = vsapi->getFrameHeight(src, plane);
            const int srcStride = vsapi->getStride(src, plane) / sizeof(T);
            const int stride = width + 16;
            const T * srcp = reinterpret_cast<const T *>(vsapi->getReadPtr(src, plane));
            T * VS_RESTRICT dstp = reinterpret_cast<T *>(vsapi->getWritePtr(dst, plane));

            uint16_t * VS_RESTRICT p_src = buffer + stride * 8;
            int * VS_RESTRICT block = reinterpret_cast<int *>(buffer);
            int * VS_RESTRICT temp = reinterpret_cast<int *>(buffer + 32);

            for (int y = 0; y < height; y++) {
                const int index = stride * (8 + y) + 8;
                std::copy_n(srcp + srcStride * y, width, p_src + index);
                for (int x = 0; x < 8; x++) {
                    p_src[index - 1 - x] = p_src[index + x];
                    p_src[index + width + x] = p_src[index + width - 1 - x];
                }
            }
            for (int y = 0; y < 8; y++) {
                memcpy(p_src + stride * (7 - y), p_src + stride * (8 + y), stride * sizeof(uint16_t));
                memcpy(p_src + stride * (height + 8 + y), p_src + stride * (height + 7 - y), stride * sizeof(uint16_t));
            }

            for (int y = 0; y < height; y++) {
                for (int x = -8; x < 0; x += 4) {
                    const int index = (stride + 1) * (8 - 3) + stride * y + 8 + x;
                    int * VS_RESTRICT tp = temp + 4 * x;

                    dctA<uint16_t, int, 1>(p_src + index, tp + 4 * 8, stride);
                }

                for (int x = 0; x < width; x++) {
                    const int index = (stride + 1) * (8 - 3) + stride * y + 8 + x;
                    int * VS_RESTRICT tp = temp + 4 * x;

                    if (!(x & 3))
                        dctA<uint16_t, int, 1>(p_src + index, tp + 4 * 8, stride);
                    dctB(tp, block);

                    int64_t v = static_cast<int64_t>(block[0]) * d->factor[0];
                    for (int i = 1; i < 16; i++) {
                        const unsigned threshold1 = d->thresh[i];
                        const unsigned threshold2 = threshold1 * 2;
                        if (block[i] + threshold1 > threshold2) {
                            if (block[i] + threshold2 > threshold2 * 2) {
                                v += static_cast<int64_t>(block[i]) * d->factor[i];
                            } else {
                                if (block[i] > 0)
                                    v += 2LL * (block[i] - static_cast<int>(threshold1)) * d->factor[i];
                                else
                                    v += 2LL * (block[i] + static_cast<int>(threshold1)) * d->factor[i];
                            }
                        }
                    }

                    dstp[srcStride * y + x] = static_cast<T>((v + (1 << 17)) >> 18);
                }
            }
        }
    }
}

template<>
void pp7Filter<float>(const VSFrameRef * src, VSFrameRef * dst, const DeblockPP7Data * const VS_RESTRICT d, const VSAPI * vsapi) noexcept {
    const auto threadId = std::this_thread::get_id();
    float * buffer = d->bufferF.at(threadId);

    for (int plane = 0; plane < d->vi->format->numPlanes; plane++) {
        if (d->process[plane]) {
            const int width = vsapi->getFrameWidth(src, plane);
            const int height = vsapi->getFrameHeight(src, plane);
            const int srcStride = vsapi->getStride(src, plane) / sizeof(float);
            const int stride = width + 16;
            const float * srcp = reinterpret_cast<const float *>(vsapi->getReadPtr(src, plane));
            float * VS_RESTRICT dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, plane));

            float * VS_RESTRICT p_src = buffer + stride * 8;
            float * VS_RESTRICT block = reinterpret_cast<float *>(buffer);
            float * VS_RESTRICT temp = reinterpret_cast<float *>(buffer + 16);

            for (int y = 0; y < height; y++) {
                const int index = stride * (8 + y) + 8;
                std::copy_n(srcp + srcStride * y, width, p_src + index);
                for (int x = 0; x < 8; x++) {
                    p_src[index - 1 - x] = p_src[index + x];
                    p_src[index + width + x] = p_src[index + width - 1 - x];
                }
            }
            for (int y = 0; y < 8; y++) {
                memcpy(p_src + stride * (7 - y), p_src + stride * (8 + y), stride * sizeof(float));
                memcpy(p_src + stride * (height + 8 + y), p_src + stride * (height + 7 - y), stride * sizeof(float));
            }

            for (int y = 0; y < height; y++) {
                for (int x = -8; x < 0; x += 4) {
                    const int index = (stride + 1) * (8 - 3) + stride * y + 8 + x;
                    float * VS_RESTRICT tp = temp + 4 * x;

                    dctA<float, float, 255>(p_src + index, tp + 4 * 8, stride);
                }

                for (int x = 0; x < width; x++) {
                    const int index = (stride + 1) * (8 - 3) + stride * y + 8 + x;
                    float * VS_RESTRICT tp = temp + 4 * x;

                    if (!(x & 3))
                        dctA<float, float, 255>(p_src + index, tp + 4 * 8, stride);
                    dctB(tp, block);

                    float v = block[0] * d->factor[0];
                    for (int i = 1; i < 16; i++) {
                        const unsigned threshold1 = d->thresh[i];
                        const unsigned threshold2 = threshold1 * 2;
                        if (static_cast<unsigned>(block[i]) + threshold1 > threshold2) {
                            if (static_cast<unsigned>(block[i]) + threshold2 > threshold2 * 2) {
                                v += block[i] * d->factor[i];
                            } else {
                                if (block[i] > 0.f)
                                    v += 2.f * (block[i] - threshold1) * d->factor[i];
                                else
                                    v += 2.f * (block[i] + threshold1) * d->factor[i];
                            }
                        }
                    }

                    dstp[srcStride * y + x] = v * (1.f / (1 << 18)) * (1.f / 255.f);
                }
            }
        }
    }
}

static void VS_CC pp7Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    DeblockPP7Data * d = static_cast<DeblockPP7Data *>(*instanceData);
    vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC pp7GetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DeblockPP7Data * d = static_cast<DeblockPP7Data *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        try {
            auto threadId = std::this_thread::get_id();

            if (!d->bufferI.count(threadId)) {
                if (d->vi->format->sampleType == stInteger) {
                    uint16_t * buffer = new (std::nothrow) uint16_t[(d->vi->width + 16) * (d->vi->height + 16 + 8)];
                    if (!buffer)
                        throw std::string{ "malloc failure (buffer)" };
                    d->bufferI.emplace(threadId, buffer);
                } else {
                    d->bufferI.emplace(threadId, nullptr);
                }
            }

            if (!d->bufferF.count(threadId)) {
                if (d->vi->format->sampleType == stFloat) {
                    float * buffer = new (std::nothrow) float[(d->vi->width + 16) * (d->vi->height + 16 + 8)];
                    if (!buffer)
                        throw std::string{ "malloc failure (buffer)" };
                    d->bufferF.emplace(threadId, buffer);
                } else {
                    d->bufferF.emplace(threadId, nullptr);
                }
            }
        } catch (const std::string & error) {
            vsapi->setFilterError(("DeblockPP7: " + error).c_str(), frameCtx);
            return nullptr;
        }

        const VSFrameRef * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef * fr[] = { d->process[0] ? nullptr : src, d->process[1] ? nullptr : src, d->process[2] ? nullptr : src };
        const int pl[] = { 0, 1, 2 };
        VSFrameRef * dst = vsapi->newVideoFrame2(d->vi->format, d->vi->width, d->vi->height, fr, pl, src, core);

        if (d->vi->format->bytesPerSample == 1)
            pp7Filter<uint8_t>(src, dst, d, vsapi);
        else if (d->vi->format->bytesPerSample == 2)
            pp7Filter<uint16_t>(src, dst, d, vsapi);
        else
            pp7Filter<float>(src, dst, d, vsapi);

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC pp7Free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    DeblockPP7Data * d = static_cast<DeblockPP7Data *>(instanceData);

    vsapi->freeNode(d->node);

    for (auto & iter : d->bufferI)
        delete[] iter.second;

    for (auto & iter : d->bufferF)
        delete[] iter.second;

    delete d;
}

static void VS_CC pp7Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DeblockPP7Data> d{ new DeblockPP7Data{} };
    int err;

    d->node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node);

    const int padWidth = (d->vi->width & 15) ? 16 - d->vi->width % 16 : 0;
    const int padHeight = (d->vi->height & 15) ? 16 - d->vi->height % 16 : 0;

    try {
        if (!isConstantFormat(d->vi) || (d->vi->format->sampleType == stInteger && d->vi->format->bitsPerSample > 16) ||
            (d->vi->format->sampleType == stFloat && d->vi->format->bitsPerSample != 32))
            throw std::string{ "only constant format 8-16 bit integer and 32 bit float input supported" };

        int qp = int64ToIntS(vsapi->propGetInt(in, "qp", 0, &err));
        if (err)
            qp = 5;

        const int m = vsapi->propNumElements(in, "planes");

        for (int i = 0; i < 3; i++)
            d->process[i] = (m <= 0);

        for (int i = 0; i < m; i++) {
            const int n = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

            if (n < 0 || n >= d->vi->format->numPlanes)
                throw std::string{ "plane index out of range" };

            if (d->process[n])
                throw std::string{ "plane specified twice" };

            d->process[n] = true;
        }

        if (qp < 1 || qp > 63)
            throw std::string{ "qp must be between 1 and 63 (inclusive)" };

        if (padWidth || padHeight) {
            VSMap * args = vsapi->createMap();
            vsapi->propSetNode(args, "clip", d->node, paReplace);
            vsapi->freeNode(d->node);
            vsapi->propSetInt(args, "width", d->vi->width + padWidth, paReplace);
            vsapi->propSetInt(args, "height", d->vi->height + padHeight, paReplace);
            vsapi->propSetFloat(args, "src_width", d->vi->width + padWidth, paReplace);
            vsapi->propSetFloat(args, "src_height", d->vi->height + padHeight, paReplace);

            VSMap * ret = vsapi->invoke(vsapi->getPluginById("com.vapoursynth.resize", core), "Point", args);
            if (vsapi->getError(ret)) {
                vsapi->setError(out, vsapi->getError(ret));
                vsapi->freeMap(args);
                vsapi->freeMap(ret);
                return;
            }

            d->node = vsapi->propGetNode(ret, "clip", 0, nullptr);
            d->vi = vsapi->getVideoInfo(d->node);
            vsapi->freeMap(args);
            vsapi->freeMap(ret);
        }

        const unsigned numThreads = vsapi->getCoreInfo(core)->numThreads;
        d->bufferI.reserve(numThreads);
        d->bufferF.reserve(numThreads);

        const int peak = (d->vi->format->sampleType == stInteger) ? (1 << d->vi->format->bitsPerSample) - 1 : 255;

        for (int i = 0; i < 16; i++)
            d->thresh[i] = static_cast<unsigned>((((i & 1) ? SN2 : SN0) * ((i & 4) ? SN2 : SN0) * qp * (1 << 2) - 1) * peak / 255);
    } catch (const std::string & error) {
        vsapi->setError(out, ("DeblockPP7: " + error).c_str());
        vsapi->freeNode(d->node);
        return;
    }

    vsapi->createFilter(in, out, "DeblockPP7", pp7Init, pp7GetFrame, pp7Free, fmParallel, 0, d.release(), core);

    if (padWidth || padHeight) {
        VSNodeRef * node = vsapi->propGetNode(out, "clip", 0, nullptr);
        vsapi->clearMap(out);

        VSMap * args = vsapi->createMap();
        vsapi->propSetNode(args, "clip", node, paReplace);
        vsapi->freeNode(node);
        vsapi->propSetInt(args, "right", padWidth, paReplace);
        vsapi->propSetInt(args, "bottom", padHeight, paReplace);

        VSMap * ret = vsapi->invoke(vsapi->getPluginById("com.vapoursynth.std", core), "Crop", args);
        if (vsapi->getError(ret)) {
            vsapi->setError(out, vsapi->getError(ret));
            vsapi->freeMap(args);
            vsapi->freeMap(ret);
            return;
        }

        node = vsapi->propGetNode(ret, "clip", 0, nullptr);
        vsapi->freeMap(args);
        vsapi->freeMap(ret);
        vsapi->propSetNode(out, "clip", node, paReplace);
        vsapi->freeNode(node);
    }
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.holywu.pp7", "pp7", "Postprocess 7 from MPlayer", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("DeblockPP7",
                 "clip:clip;"
                 "qp:int:opt;"
                 "planes:int[]:opt;",
                 pp7Create, nullptr, plugin);
}
