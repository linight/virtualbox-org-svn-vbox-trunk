/* $Id$ */
/** @file
 * Encodes the screen content in VPX format.
 */

/*
 * Copyright (C) 2012-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define LOG_GROUP LOG_GROUP_MAIN

#include <vector>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <VBox/com/VirtualBox.h>
#include <VBox/com/com.h>
#include <VBox/com/string.h>

#include "EbmlWriter.h"
#include "VideoRec.h"

#define VPX_CODEC_DISABLE_COMPAT 1
#include <vpx/vp8cx.h>
#include <vpx/vpx_image.h>

/** Default VPX codec to use. */
#define DEFAULTCODEC (vpx_codec_vp8_cx())

static int videoRecEncodeAndWrite(PVIDEORECSTREAM pStrm);
static int videoRecRGBToYUV(PVIDEORECSTREAM pStrm);

using namespace com;

/**
 * Enumeration for a video recording state.
 */
enum
{
    /** Not initialized. */
    VIDREC_UNINITIALIZED = 0,
    /** Initialized, idle. */
    VIDREC_IDLE = 1,
    /** Currently in VideoRecCopyToIntBuf(), delay termination. */
    VIDREC_COPYING = 2,
    /** Signal that we are terminating. */
    VIDREC_TERMINATING = 3
};

/* Must be always accessible and therefore cannot be part of VIDEORECCONTEXT */
static uint32_t g_enmState = VIDREC_UNINITIALIZED;

/**
 * Structure for keeping specific video recording codec data.
 */
typedef struct VIDEORECCODEC
{
    union
    {
        struct
        {
            /** VPX codec context. */
            vpx_codec_ctx_t     CodecCtx;
            /** VPX codec configuration. */
            vpx_codec_enc_cfg_t Config;
            /** VPX image context. */
            vpx_image_t         RawImage;
        } VPX;
    };
} VIDEORECCODEC, *PVIDEORECCODEC;

/**
 * Strucutre for maintaining a video recording stream.
 */
typedef struct VIDEORECSTREAM
{
    /** Container context. */
    WebMWriter         *pEBML;
    /** Codec data. */
    VIDEORECCODEC       Codec;
    /** Target X resolution (in pixels). */
    uint32_t            uTargetWidth;
    /** Target Y resolution (in pixels). */
    uint32_t            uTargetHeight;
    /** X resolution of the last encoded frame. */
    uint32_t            uLastSourceWidth;
    /** Y resolution of the last encoded frame. */
    uint32_t            uLastSourceHeight;
    /** Current frame number. */
    uint64_t            cFrame;
    /** RGB buffer containing the most recent frame of the framebuffer. */
    uint8_t            *pu8RgbBuf;
    /** YUV buffer the encode function fetches the frame from. */
    uint8_t            *pu8YuvBuf;
    /** Whether video recording is enabled or not. */
    bool                fEnabled;
    /** Whether the RGB buffer is filled or not. */
    bool                fRgbFilled;
    /** Pixel format of the current frame. */
    uint32_t            u32PixelFormat;
    /** Minimal delay between two frames. */
    uint32_t            uDelay;
    /** Time stamp of the last frame we encoded. */
    uint64_t            u64LastTimeStamp;
    /** Time stamp of the current frame. */
    uint64_t            u64TimeStamp;
    /** Encoder deadline. */
    unsigned int        uEncoderDeadline;
} VIDEORECSTREAM, *PVIDEORECSTREAM;

/** Vector of video recording streams. */
typedef std::vector <PVIDEORECSTREAM> VideoRecStreams;

/**
 * Structure for keeping a video recording context.
 */
typedef struct VIDEORECCONTEXT
{
    /** Semaphore to signal the encoding worker thread. */
    RTSEMEVENT          WaitEvent;
    /** Semaphore required during termination. */
    RTSEMEVENT          TermEvent;
    /** Whether video recording is enabled or not. */
    bool                fEnabled;
    /** Worker thread. */
    RTTHREAD            Thread;
    /** Maximal time stamp. */
    uint64_t            u64MaxTimeStamp;
    /** Maximal file size in MB. */
    uint32_t            uMaxFileSize;
    /** Vector of current video recording stream contexts. */
    VideoRecStreams     vecStreams;
} VIDEORECCONTEXT, *PVIDEORECCONTEXT;


/**
 * Iterator class for running through a BGRA32 image buffer and converting
 * it to RGB.
 */
class ColorConvBGRA32Iter
{
private:
    enum { PIX_SIZE = 4 };
public:
    ColorConvBGRA32Iter(unsigned aWidth, unsigned aHeight, uint8_t *aBuf)
    {
        LogFlow(("width = %d height=%d aBuf=%lx\n", aWidth, aHeight, aBuf));
        mPos = 0;
        mSize = aWidth * aHeight * PIX_SIZE;
        mBuf = aBuf;
    }
    /**
     * Convert the next pixel to RGB.
     * @returns true on success, false if we have reached the end of the buffer
     * @param   aRed    where to store the red value
     * @param   aGreen  where to store the green value
     * @param   aBlue   where to store the blue value
     */
    bool getRGB(unsigned *aRed, unsigned *aGreen, unsigned *aBlue)
    {
        bool rc = false;
        if (mPos + PIX_SIZE <= mSize)
        {
            *aRed   = mBuf[mPos + 2];
            *aGreen = mBuf[mPos + 1];
            *aBlue  = mBuf[mPos    ];
            mPos += PIX_SIZE;
            rc = true;
        }
        return rc;
    }

    /**
     * Skip forward by a certain number of pixels
     * @param aPixels  how many pixels to skip
     */
    void skip(unsigned aPixels)
    {
        mPos += PIX_SIZE * aPixels;
    }
private:
    /** Size of the picture buffer */
    unsigned mSize;
    /** Current position in the picture buffer */
    unsigned mPos;
    /** Address of the picture buffer */
    uint8_t *mBuf;
};

/**
 * Iterator class for running through an BGR24 image buffer and converting
 * it to RGB.
 */
class ColorConvBGR24Iter
{
private:
    enum { PIX_SIZE = 3 };
public:
    ColorConvBGR24Iter(unsigned aWidth, unsigned aHeight, uint8_t *aBuf)
    {
        mPos = 0;
        mSize = aWidth * aHeight * PIX_SIZE;
        mBuf = aBuf;
    }
    /**
     * Convert the next pixel to RGB.
     * @returns true on success, false if we have reached the end of the buffer
     * @param   aRed    where to store the red value
     * @param   aGreen  where to store the green value
     * @param   aBlue   where to store the blue value
     */
    bool getRGB(unsigned *aRed, unsigned *aGreen, unsigned *aBlue)
    {
        bool rc = false;
        if (mPos + PIX_SIZE <= mSize)
        {
            *aRed   = mBuf[mPos + 2];
            *aGreen = mBuf[mPos + 1];
            *aBlue  = mBuf[mPos    ];
            mPos += PIX_SIZE;
            rc = true;
        }
        return rc;
    }

    /**
     * Skip forward by a certain number of pixels
     * @param aPixels  how many pixels to skip
     */
    void skip(unsigned aPixels)
    {
        mPos += PIX_SIZE * aPixels;
    }
private:
    /** Size of the picture buffer */
    unsigned mSize;
    /** Current position in the picture buffer */
    unsigned mPos;
    /** Address of the picture buffer */
    uint8_t *mBuf;
};

/**
 * Iterator class for running through an BGR565 image buffer and converting
 * it to RGB.
 */
class ColorConvBGR565Iter
{
private:
    enum { PIX_SIZE = 2 };
public:
    ColorConvBGR565Iter(unsigned aWidth, unsigned aHeight, uint8_t *aBuf)
    {
        mPos = 0;
        mSize = aWidth * aHeight * PIX_SIZE;
        mBuf = aBuf;
    }
    /**
     * Convert the next pixel to RGB.
     * @returns true on success, false if we have reached the end of the buffer
     * @param   aRed    where to store the red value
     * @param   aGreen  where to store the green value
     * @param   aBlue   where to store the blue value
     */
    bool getRGB(unsigned *aRed, unsigned *aGreen, unsigned *aBlue)
    {
        bool rc = false;
        if (mPos + PIX_SIZE <= mSize)
        {
            unsigned uFull =  (((unsigned) mBuf[mPos + 1]) << 8)
                             | ((unsigned) mBuf[mPos]);
            *aRed   = (uFull >> 8) & ~7;
            *aGreen = (uFull >> 3) & ~3 & 0xff;
            *aBlue  = (uFull << 3) & ~7 & 0xff;
            mPos += PIX_SIZE;
            rc = true;
        }
        return rc;
    }

    /**
     * Skip forward by a certain number of pixels
     * @param aPixels  how many pixels to skip
     */
    void skip(unsigned aPixels)
    {
        mPos += PIX_SIZE * aPixels;
    }
private:
    /** Size of the picture buffer */
    unsigned mSize;
    /** Current position in the picture buffer */
    unsigned mPos;
    /** Address of the picture buffer */
    uint8_t *mBuf;
};

/**
 * Convert an image to YUV420p format
 * @returns true on success, false on failure
 * @param aWidth    width of image
 * @param aHeight   height of image
 * @param aDestBuf  an allocated memory buffer large enough to hold the
 *                  destination image (i.e. width * height * 12bits)
 * @param aSrcBuf   the source image as an array of bytes
 */
template <class T>
inline bool colorConvWriteYUV420p(unsigned aWidth, unsigned aHeight, uint8_t *aDestBuf, uint8_t *aSrcBuf)
{
    AssertReturn(!(aWidth & 1), false);
    AssertReturn(!(aHeight & 1), false);
    bool fRc = true;
    T iter1(aWidth, aHeight, aSrcBuf);
    T iter2 = iter1;
    iter2.skip(aWidth);
    unsigned cPixels = aWidth * aHeight;
    unsigned offY = 0;
    unsigned offU = cPixels;
    unsigned offV = cPixels + cPixels / 4;
    unsigned const cyHalf = aHeight / 2;
    unsigned const cxHalf = aWidth  / 2;
    for (unsigned i = 0; i < cyHalf && fRc; ++i)
    {
        for (unsigned j = 0; j < cxHalf; ++j)
        {
            unsigned red, green, blue;
            fRc = iter1.getRGB(&red, &green, &blue);
            AssertReturn(fRc, false);
            aDestBuf[offY] = ((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16;
            unsigned u = (((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128) / 4;
            unsigned v = (((112 * red - 94 * green -  18 * blue + 128) >> 8) + 128) / 4;

            fRc = iter1.getRGB(&red, &green, &blue);
            AssertReturn(fRc, false);
            aDestBuf[offY + 1] = ((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16;
            u += (((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128) / 4;
            v += (((112 * red - 94 * green -  18 * blue + 128) >> 8) + 128) / 4;

            fRc = iter2.getRGB(&red, &green, &blue);
            AssertReturn(fRc, false);
            aDestBuf[offY + aWidth] = ((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16;
            u += (((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128) / 4;
            v += (((112 * red - 94 * green -  18 * blue + 128) >> 8) + 128) / 4;

            fRc = iter2.getRGB(&red, &green, &blue);
            AssertReturn(fRc, false);
            aDestBuf[offY + aWidth + 1] = ((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16;
            u += (((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128) / 4;
            v += (((112 * red - 94 * green -  18 * blue + 128) >> 8) + 128) / 4;

            aDestBuf[offU] = u;
            aDestBuf[offV] = v;
            offY += 2;
            ++offU;
            ++offV;
        }

        iter1.skip(aWidth);
        iter2.skip(aWidth);
        offY += aWidth;
    }

    return true;
}

/**
 * Convert an image to RGB24 format
 * @returns true on success, false on failure
 * @param aWidth    width of image
 * @param aHeight   height of image
 * @param aDestBuf  an allocated memory buffer large enough to hold the
 *                  destination image (i.e. width * height * 12bits)
 * @param aSrcBuf   the source image as an array of bytes
 */
template <class T>
inline bool colorConvWriteRGB24(unsigned aWidth, unsigned aHeight,
                                uint8_t *aDestBuf, uint8_t *aSrcBuf)
{
    enum { PIX_SIZE = 3 };
    bool rc = true;
    AssertReturn(0 == (aWidth & 1), false);
    AssertReturn(0 == (aHeight & 1), false);
    T iter(aWidth, aHeight, aSrcBuf);
    unsigned cPixels = aWidth * aHeight;
    for (unsigned i = 0; i < cPixels && rc; ++i)
    {
        unsigned red, green, blue;
        rc = iter.getRGB(&red, &green, &blue);
        if (rc)
        {
            aDestBuf[i * PIX_SIZE    ] = red;
            aDestBuf[i * PIX_SIZE + 1] = green;
            aDestBuf[i * PIX_SIZE + 2] = blue;
        }
    }
    return rc;
}

/**
 * Worker thread for all streams of a video recording context.
 *
 * Does RGB/YUV conversion and encoding.
 */
static DECLCALLBACK(int) videoRecThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    PVIDEORECCONTEXT pCtx = (PVIDEORECCONTEXT)pvUser;
    for (;;)
    {
        int rc = RTSemEventWait(pCtx->WaitEvent, RT_INDEFINITE_WAIT);
        AssertRCBreak(rc);

        if (ASMAtomicReadU32(&g_enmState) == VIDREC_TERMINATING)
            break;

        for (VideoRecStreams::iterator it = pCtx->vecStreams.begin(); it != pCtx->vecStreams.end(); it++)
        {
            PVIDEORECSTREAM pStream = (*it);

            if (   pStream->fEnabled
                && ASMAtomicReadBool(&pStream->fRgbFilled))
            {
                rc = videoRecRGBToYUV(pStream);

                ASMAtomicWriteBool(&pStream->fRgbFilled, false);

                if (RT_SUCCESS(rc))
                    rc = videoRecEncodeAndWrite(pStream);

                if (RT_FAILURE(rc))
                {
                    static unsigned cErrors = 100;
                    if (cErrors > 0)
                    {
                        LogRel(("VideoRec: Error %Rrc encoding / writing video frame\n", rc));
                        cErrors--;
                    }
                }
            }
        }
    }

    return VINF_SUCCESS;
}

/**
 * Creates a video recording context.
 *
 * @returns IPRT status code.
 * @param   cScreens         Number of screens to create context for.
 * @param   ppCtx            Pointer to created video recording context on success.
 */
int VideoRecContextCreate(uint32_t cScreens, PVIDEORECCONTEXT *ppCtx)
{
    AssertReturn(cScreens, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppCtx, VERR_INVALID_POINTER);

    Assert(ASMAtomicReadU32(&g_enmState) == VIDREC_UNINITIALIZED);

    int rc = VINF_SUCCESS;

    PVIDEORECCONTEXT pCtx = (PVIDEORECCONTEXT)RTMemAllocZ(sizeof(VIDEORECCONTEXT));
    if (!pCtx)
        return VERR_NO_MEMORY;

    for (uint32_t uScreen = 0; uScreen < cScreens; uScreen++)
    {
        PVIDEORECSTREAM pStream = (PVIDEORECSTREAM)RTMemAllocZ(sizeof(VIDEORECSTREAM));
        if (!pStream)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        try
        {
            pCtx->vecStreams.push_back(pStream);

            pStream->pEBML = new WebMWriter();
        }
        catch (std::bad_alloc)
        {
            rc = VERR_NO_MEMORY;
            break;
        }
    }

    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventCreate(&pCtx->WaitEvent);
        AssertRCReturn(rc, rc);

        rc = RTSemEventCreate(&pCtx->TermEvent);
        AssertRCReturn(rc, rc);

        rc = RTThreadCreate(&pCtx->Thread, videoRecThread, (void*)pCtx, 0,
                            RTTHREADTYPE_MAIN_WORKER, RTTHREADFLAGS_WAITABLE, "VideoRec");
        AssertRCReturn(rc, rc);

        ASMAtomicWriteU32(&g_enmState, VIDREC_IDLE);

        if (ppCtx)
            *ppCtx = pCtx;
    }
    else
    {
        /* Roll back allocations on error. */
        VideoRecStreams::iterator it = pCtx->vecStreams.begin();
        while (it != pCtx->vecStreams.end())
        {
            PVIDEORECSTREAM pStream = (*it);

            if (pStream->pEBML)
                delete pStream->pEBML;

            it = pCtx->vecStreams.erase(it);

            RTMemFree(pStream);
            pStream = NULL;
        }

        Assert(pCtx->vecStreams.empty());
    }

    return rc;
}

/**
 * Destroys a video recording context.
 *
 * @param pCtx                  Video recording context to destroy.
 */
void VideoRecContextDestroy(PVIDEORECCONTEXT pCtx)
{
    if (!pCtx)
        return;

    uint32_t enmState = VIDREC_IDLE;

    for (;;) /** @todo r=andy Remove busy waiting! */
    {
        if (ASMAtomicCmpXchgExU32(&g_enmState, VIDREC_TERMINATING, enmState, &enmState))
            break;
        if (enmState == VIDREC_UNINITIALIZED)
            return;
    }

    if (enmState == VIDREC_COPYING)
    {
        int rc = RTSemEventWait(pCtx->TermEvent, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }

    RTSemEventSignal(pCtx->WaitEvent);
    RTThreadWait(pCtx->Thread, 10 * 1000, NULL);
    RTSemEventDestroy(pCtx->WaitEvent);
    RTSemEventDestroy(pCtx->TermEvent);

    VideoRecStreams::iterator it = pCtx->vecStreams.begin();
    while (it != pCtx->vecStreams.end())
    {
        PVIDEORECSTREAM pStream = (*it);

        if (pStream->fEnabled)
        {
            AssertPtr(pStream->pEBML);
            pStream->pEBML->Close();

            vpx_img_free(&pStream->Codec.VPX.RawImage);
            vpx_codec_err_t rcv = vpx_codec_destroy(&pStream->Codec.VPX.CodecCtx);
            Assert(rcv == VPX_CODEC_OK); RT_NOREF(rcv);

            if (pStream->pu8RgbBuf)
            {
                RTMemFree(pStream->pu8RgbBuf);
                pStream->pu8RgbBuf = NULL;
            }
        }

        if (pStream->pEBML)
        {
            delete pStream->pEBML;
            pStream->pEBML = NULL;
        }

        it = pCtx->vecStreams.erase(it);

        RTMemFree(pStream);
        pStream = NULL;
    }

    Assert(pCtx->vecStreams.empty());
    RTMemFree(pCtx);

    ASMAtomicWriteU32(&g_enmState, VIDREC_UNINITIALIZED);
}

/**
 * VideoRec utility function to initialize video recording context.
 *
 * @returns IPRT status code.
 * @param   pCtx                Pointer to video recording context to initialize Framebuffer width.
 * @param   uScreen             Screen number.
 * @param   pszFile             File to save the recorded data
 * @param   uWidth              Width of the target image in the video recoriding file (movie)
 * @param   uHeight             Height of the target image in video recording file.
 * @param   uRate               Rate.
 * @param   uFps                FPS.
 * @param   uMaxTime
 * @param   uMaxFileSize
 * @param   pszOptions
 */
int VideoRecStreamInit(PVIDEORECCONTEXT pCtx, uint32_t uScreen, const char *pszFile,
                       uint32_t uWidth, uint32_t uHeight, uint32_t uRate, uint32_t uFps,
                       uint32_t uMaxTime, uint32_t uMaxFileSize, const char *pszOptions)
{
    AssertPtrReturn(pCtx,                           VERR_INVALID_PARAMETER);
    AssertReturn(uScreen < pCtx->vecStreams.size(), VERR_INVALID_PARAMETER);

    pCtx->u64MaxTimeStamp = (uMaxTime > 0 ? RTTimeProgramMilliTS() + uMaxTime * 1000 : 0);
    pCtx->uMaxFileSize = uMaxFileSize;

    PVIDEORECSTREAM pStream = pCtx->vecStreams.at(uScreen);

    pStream->uTargetWidth  = uWidth;
    pStream->uTargetHeight = uHeight;
    pStream->pu8RgbBuf = (uint8_t *)RTMemAllocZ(uWidth * uHeight * 4);
    AssertReturn(pStream->pu8RgbBuf, VERR_NO_MEMORY);
    pStream->uEncoderDeadline = VPX_DL_REALTIME;

    /* Play safe: the file must not exist, overwriting is potentially
     * hazardous as nothing prevents the user from picking a file name of some
     * other important file, causing unintentional data loss. */

    vpx_codec_err_t rcv = vpx_codec_enc_config_default(DEFAULTCODEC, &pStream->Codec.VPX.Config, 0);
    if (rcv != VPX_CODEC_OK)
    {
        LogRel(("VideoRec: Failed to get default configuration for VPX codec: %s\n", vpx_codec_err_to_string(rcv)));
        return VERR_INVALID_PARAMETER;
    }

    com::Utf8Str options(pszOptions);
    size_t pos = 0;

    /* By default we enable everything (if available). */
    bool fHasVideoTrack = true;
#ifdef VBOX_WITH_AUDIO_VIDEOREC
    bool fHasAudioTrack = true;
#endif

    com::Utf8Str key, value;
    while ((pos = options.parseKeyValue(key, value, pos)) != com::Utf8Str::npos)
    {
        if (key.compare("vc_quality", Utf8Str::CaseInsensitive) == 0)
        {
            if (value.compare("realtime", Utf8Str::CaseInsensitive) == 0)
            {
                pStream->uEncoderDeadline = VPX_DL_REALTIME;
            }
            else if (value.compare("good", Utf8Str::CaseInsensitive) == 0)
            {
                pStream->uEncoderDeadline = 1000000 / uFps;
            }
            else if (value.compare("best", Utf8Str::CaseInsensitive) == 0)
            {
                pStream->uEncoderDeadline = VPX_DL_BEST_QUALITY;
            }
            else
            {
                LogRel(("VideoRec: Setting quality deadline to '%s'\n", value.c_str()));
                pStream->uEncoderDeadline = value.toUInt32();
            }
        }
        else if (key.compare("vc_enabled", Utf8Str::CaseInsensitive) == 0)
        {
#ifdef VBOX_WITH_AUDIO_VIDEOREC
            if (value.compare("false", Utf8Str::CaseInsensitive) == 0) /* Disable audio. */
            {
                fHasVideoTrack = false;
                LogRel(("VideoRec: Only audio will be recorded\n"));
            }
#endif
        }
        else if (key.compare("ac_enabled", Utf8Str::CaseInsensitive) == 0)
        {
#ifdef VBOX_WITH_AUDIO_VIDEOREC
            if (value.compare("false", Utf8Str::CaseInsensitive)) /* Disable audio. */
            {
                fHasAudioTrack = false;
                LogRel(("VideoRec: Only video will be recorded\n"));
            }
#endif
        }
        else
            LogRel(("VideoRec: Unknown option '%s' (value '%s'), skipping\n", key.c_str(), value.c_str()));

    } /* while */

    uint64_t fOpen = RTFILE_O_WRITE | RTFILE_O_DENY_WRITE;
#ifdef DEBUG
    fOpen |= RTFILE_O_CREATE_REPLACE;
#else
    fOpen |= RTFILE_O_CREATE;
#endif

    int rc = pStream->pEBML->Create(pszFile, fOpen, WebMWriter::AudioCodec_Opus, WebMWriter::VideoCodec_VP8);
    if (RT_FAILURE(rc))
    {
        LogRel(("VideoRec: Failed to create the video capture output file '%s' (%Rrc)\n", pszFile, rc));
        return rc;
    }

    /* target bitrate in kilobits per second */
    pStream->Codec.VPX.Config.rc_target_bitrate = uRate;
    /* frame width */
    pStream->Codec.VPX.Config.g_w = uWidth;
    /* frame height */
    pStream->Codec.VPX.Config.g_h = uHeight;
    /* 1ms per frame */
    pStream->Codec.VPX.Config.g_timebase.num = 1;
    pStream->Codec.VPX.Config.g_timebase.den = 1000;
    /* disable multithreading */
    pStream->Codec.VPX.Config.g_threads = 0;

    pStream->uDelay = 1000 / uFps;

    if (fHasVideoTrack)
    {
        rc = pStream->pEBML->AddVideoTrack(pStream->Codec.VPX.Config.g_w, /* Width */
                                           pStream->Codec.VPX.Config.g_h, /* Height */
                                           uFps);
        if (RT_FAILURE(rc))
        {
            LogRel(("VideoRec: Failed to add video track to output file '%s' (%Rrc)\n", pszFile, rc));
            return rc;
        }
    }

    /* Initialize codec */
    rcv = vpx_codec_enc_init(&pStream->Codec.VPX.CodecCtx, DEFAULTCODEC, &pStream->Codec.VPX.Config, 0);
    if (rcv != VPX_CODEC_OK)
    {
        LogFlow(("Failed to initialize VP8 encoder %s", vpx_codec_err_to_string(rcv)));
        return VERR_INVALID_PARAMETER;
    }

    if (!vpx_img_alloc(&pStream->Codec.VPX.RawImage, VPX_IMG_FMT_I420, uWidth, uHeight, 1))
    {
        LogFlow(("Failed to allocate image %dx%d", uWidth, uHeight));
        return VERR_NO_MEMORY;
    }

    pStream->pu8YuvBuf = pStream->Codec.VPX.RawImage.planes[0];

    pCtx->fEnabled = true;
    pStream->fEnabled = true;

    return VINF_SUCCESS;
}

/**
 * VideoRec utility function to check if recording is enabled.
 *
 * @returns true if recording is enabled
 * @param   pCtx                Pointer to video recording context.
 */
bool VideoRecIsEnabled(PVIDEORECCONTEXT pCtx)
{
    RT_NOREF(pCtx);
    uint32_t enmState = ASMAtomicReadU32(&g_enmState);
    return (   enmState == VIDREC_IDLE
            || enmState == VIDREC_COPYING);
}

/**
 * VideoRec utility function to check if recording engine is ready to accept a new frame
 * for the given screen.
 *
 * @returns true if recording engine is ready
 * @param   pCtx                Pointer to video recording context.
 * @param   uScreen             Screen ID.
 * @param   u64TimeStamp        Current time stamp.
 */
bool VideoRecIsReady(PVIDEORECCONTEXT pCtx, uint32_t uScreen, uint64_t u64TimeStamp)
{
    uint32_t enmState = ASMAtomicReadU32(&g_enmState);
    if (enmState != VIDREC_IDLE)
        return false;

    PVIDEORECSTREAM pStream = pCtx->vecStreams.at(uScreen);
    if (!pStream->fEnabled)
        return false;

    if (u64TimeStamp < pStream->u64LastTimeStamp + pStream->uDelay)
        return false;

    if (ASMAtomicReadBool(&pStream->fRgbFilled))
        return false;

    return true;
}

/**
 * VideoRec utility function to check if the file size has reached
 * specified limits (if any).
 *
 * @returns true if any limit has been reached.
 * @param   pCtx                Pointer to video recording context.
 * @param   uScreen             Screen ID.
 * @param   u64TimeStamp        Current time stamp.
 */

bool VideoRecIsFull(PVIDEORECCONTEXT pCtx, uint32_t uScreen, uint64_t u64TimeStamp)
{
    PVIDEORECSTREAM pStream = pCtx->vecStreams.at(uScreen);
    if(!pStream->fEnabled)
        return false;

    if(pCtx->u64MaxTimeStamp > 0 && u64TimeStamp >= pCtx->u64MaxTimeStamp)
        return true;

    if (pCtx->uMaxFileSize > 0)
    {
        uint64_t sizeInMB = pStream->pEBML->GetFileSize() / (1024 * 1024);
        if(sizeInMB >= pCtx->uMaxFileSize)
            return true;
    }
    /* Check for available free disk space */
    if (pStream->pEBML->GetAvailableSpace() < 0x100000)
    {
        LogRel(("VideoRec: Not enough free storage space available, stopping video capture\n"));
        return true;
    }

    return false;
}

/**
 * VideoRec utility function to encode the source image and write the encoded
 * image to target file.
 *
 * @returns IPRT status code.
 * @param   pStream             Stream to encode and write.
 */
static int videoRecEncodeAndWrite(PVIDEORECSTREAM pStream)
{
    /* presentation time stamp */
    vpx_codec_pts_t pts = pStream->u64TimeStamp;
    vpx_codec_err_t rcv = vpx_codec_encode(&pStream->Codec.VPX.CodecCtx,
                                           &pStream->Codec.VPX.RawImage,
                                           pts /* time stamp */,
                                           pStream->uDelay  /* how long to show this frame */,
                                           0   /* flags */,
                                           pStream->uEncoderDeadline /* quality setting */);
    if (rcv != VPX_CODEC_OK)
    {
        LogFlow(("Failed to encode:%s\n", vpx_codec_err_to_string(rcv)));
        return VERR_GENERAL_FAILURE;
    }

    vpx_codec_iter_t iter = NULL;
    int rc = VERR_NO_DATA;
    for (;;)
    {
        const vpx_codec_cx_pkt_t *pPacket = vpx_codec_get_cx_data(&pStream->Codec.VPX.CodecCtx, &iter);
        if (!pPacket)
            break;

        switch (pPacket->kind)
        {
            case VPX_CODEC_CX_FRAME_PKT:
            {
                WebMWriter::BlockData_VP8 blockData = { &pStream->Codec.VPX.Config, pPacket };
                rc = pStream->pEBML->WriteBlock(WebMWriter::BlockType_Video, &blockData, sizeof(blockData));
                break;
            }

            default:
                LogFlow(("Unexpected CODEC packet kind %ld\n", pPacket->kind));
                break;
        }
    }

    pStream->cFrame++;
    return rc;
}

/**
 * VideoRec utility function to convert RGB to YUV.
 *
 * @returns IPRT status code.
 * @param   pStrm      Strm.
 */
static int videoRecRGBToYUV(PVIDEORECSTREAM pStrm)
{
    switch (pStrm->u32PixelFormat)
    {
        case VPX_IMG_FMT_RGB32:
            LogFlow(("32 bit\n"));
            if (!colorConvWriteYUV420p<ColorConvBGRA32Iter>(pStrm->uTargetWidth,
                                                            pStrm->uTargetHeight,
                                                            pStrm->pu8YuvBuf,
                                                            pStrm->pu8RgbBuf))
                return VERR_GENERAL_FAILURE;
            break;
        case VPX_IMG_FMT_RGB24:
            LogFlow(("24 bit\n"));
            if (!colorConvWriteYUV420p<ColorConvBGR24Iter>(pStrm->uTargetWidth,
                                                           pStrm->uTargetHeight,
                                                           pStrm->pu8YuvBuf,
                                                           pStrm->pu8RgbBuf))
                return VERR_GENERAL_FAILURE;
            break;
        case VPX_IMG_FMT_RGB565:
            LogFlow(("565 bit\n"));
            if (!colorConvWriteYUV420p<ColorConvBGR565Iter>(pStrm->uTargetWidth,
                                                            pStrm->uTargetHeight,
                                                            pStrm->pu8YuvBuf,
                                                            pStrm->pu8RgbBuf))
                return VERR_GENERAL_FAILURE;
            break;
        default:
            return VERR_GENERAL_FAILURE;
    }
    return VINF_SUCCESS;
}

/**
 * VideoRec utility function to copy a source image (FrameBuf) to the intermediate
 * RGB buffer. This function is executed only once per time.
 *
 * @thread  EMT
 *
 * @returns IPRT status code.
 * @param   pCtx               Pointer to the video recording context.
 * @param   uScreen            Screen number.
 * @param   x                  Starting x coordinate of the source buffer (Framebuffer).
 * @param   y                  Starting y coordinate of the source buffer (Framebuffer).
 * @param   uPixelFormat       Pixel Format.
 * @param   uBitsPerPixel      Bits Per Pixel
 * @param   uBytesPerLine      Bytes per source scanlineName.
 * @param   uSourceWidth       Width of the source image (framebuffer).
 * @param   uSourceHeight      Height of the source image (framebuffer).
 * @param   pu8BufAddr         Pointer to source image(framebuffer).
 * @param   u64TimeStamp       Time stamp (milliseconds).
 */
int VideoRecCopyToIntBuf(PVIDEORECCONTEXT pCtx, uint32_t uScreen, uint32_t x, uint32_t y,
                         uint32_t uPixelFormat, uint32_t uBitsPerPixel, uint32_t uBytesPerLine,
                         uint32_t uSourceWidth, uint32_t uSourceHeight, uint8_t *pu8BufAddr,
                         uint64_t u64TimeStamp)
{
    /* Do not execute during termination and guard against termination */
    if (!ASMAtomicCmpXchgU32(&g_enmState, VIDREC_COPYING, VIDREC_IDLE))
        return VINF_TRY_AGAIN;

    int rc = VINF_SUCCESS;
    do
    {
        AssertPtrBreakStmt(pu8BufAddr,                     rc = VERR_INVALID_PARAMETER);
        AssertBreakStmt(uSourceWidth,                      rc = VERR_INVALID_PARAMETER);
        AssertBreakStmt(uSourceHeight,                     rc = VERR_INVALID_PARAMETER);
        AssertBreakStmt(uScreen < pCtx->vecStreams.size(), rc = VERR_INVALID_PARAMETER);

        PVIDEORECSTREAM pStream = pCtx->vecStreams.at(uScreen);
        if (!pStream->fEnabled)
        {
            rc = VINF_TRY_AGAIN; /* not (yet) enabled */
            break;
        }
        if (u64TimeStamp < pStream->u64LastTimeStamp + pStream->uDelay)
        {
            rc = VINF_TRY_AGAIN; /* respect maximum frames per second */
            break;
        }
        if (ASMAtomicReadBool(&pStream->fRgbFilled))
        {
            rc = VERR_TRY_AGAIN; /* previous frame not yet encoded */
            break;
        }

        pStream->u64LastTimeStamp = u64TimeStamp;

        int xDiff = ((int)pStream->uTargetWidth - (int)uSourceWidth) / 2;
        uint32_t w = uSourceWidth;
        if ((int)w + xDiff + (int)x <= 0)  /* nothing visible */
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        uint32_t destX;
        if ((int)x < -xDiff)
        {
            w += xDiff + x;
            x = -xDiff;
            destX = 0;
        }
        else
            destX = x + xDiff;

        uint32_t h = uSourceHeight;
        int yDiff = ((int)pStream->uTargetHeight - (int)uSourceHeight) / 2;
        if ((int)h + yDiff + (int)y <= 0)  /* nothing visible */
        {
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        uint32_t destY;
        if ((int)y < -yDiff)
        {
            h += yDiff + (int)y;
            y = -yDiff;
            destY = 0;
        }
        else
            destY = y + yDiff;

        if (   destX > pStream->uTargetWidth
            || destY > pStream->uTargetHeight)
        {
            rc = VERR_INVALID_PARAMETER;  /* nothing visible */
            break;
        }

        if (destX + w > pStream->uTargetWidth)
            w = pStream->uTargetWidth - destX;

        if (destY + h > pStream->uTargetHeight)
            h = pStream->uTargetHeight - destY;

        /* Calculate bytes per pixel */
        uint32_t bpp = 1;
        if (uPixelFormat == BitmapFormat_BGR)
        {
            switch (uBitsPerPixel)
            {
                case 32:
                    pStream->u32PixelFormat = VPX_IMG_FMT_RGB32;
                    bpp = 4;
                    break;
                case 24:
                    pStream->u32PixelFormat = VPX_IMG_FMT_RGB24;
                    bpp = 3;
                    break;
                case 16:
                    pStream->u32PixelFormat = VPX_IMG_FMT_RGB565;
                    bpp = 2;
                    break;
                default:
                    AssertMsgFailed(("Unknown color depth! mBitsPerPixel=%d\n", uBitsPerPixel));
                    break;
            }
        }
        else
            AssertMsgFailed(("Unknown pixel format! mPixelFormat=%d\n", uPixelFormat));

        /* One of the dimensions of the current frame is smaller than before so
         * clear the entire buffer to prevent artifacts from the previous frame */
        if (   uSourceWidth  < pStream->uLastSourceWidth
            || uSourceHeight < pStream->uLastSourceHeight)
            memset(pStream->pu8RgbBuf, 0, pStream->uTargetWidth * pStream->uTargetHeight * 4);

        pStream->uLastSourceWidth  = uSourceWidth;
        pStream->uLastSourceHeight = uSourceHeight;

        /* Calculate start offset in source and destination buffers */
        uint32_t offSrc = y * uBytesPerLine + x * bpp;
        uint32_t offDst = (destY * pStream->uTargetWidth + destX) * bpp;
        /* do the copy */
        for (unsigned int i = 0; i < h; i++)
        {
            /* Overflow check */
            Assert(offSrc + w * bpp <= uSourceHeight * uBytesPerLine);
            Assert(offDst + w * bpp <= pStream->uTargetHeight * pStream->uTargetWidth * bpp);
            memcpy(pStream->pu8RgbBuf + offDst, pu8BufAddr + offSrc, w * bpp);
            offSrc += uBytesPerLine;
            offDst += pStream->uTargetWidth * bpp;
        }

        pStream->u64TimeStamp = u64TimeStamp;

        ASMAtomicWriteBool(&pStream->fRgbFilled, true);
        RTSemEventSignal(pCtx->WaitEvent);
    } while (0);

    if (!ASMAtomicCmpXchgU32(&g_enmState, VIDREC_IDLE, VIDREC_COPYING))
    {
        rc = RTSemEventSignal(pCtx->TermEvent);
        AssertRC(rc);
    }

    return rc;
}
