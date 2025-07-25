/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>
#include "libavutil/dict.h"
#include "libavcodec/avcodec.h"
#ifdef __GNUC__
#  include <unistd.h>
#endif

#include "FFmpegDataDecoder.h"
#include "FFmpegLog.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TaskQueue.h"
#include "prsystem.h"
#include "VideoUtils.h"
#include "FFmpegUtils.h"

#include "FFmpegLibs.h"

namespace mozilla {

StaticMutex FFmpegDataDecoder<LIBAV_VER>::sMutex;

FFmpegDataDecoder<LIBAV_VER>::FFmpegDataDecoder(FFmpegLibWrapper* aLib,
                                                AVCodecID aCodecID)
    : mLib(aLib),
      mCodecContext(nullptr),
      mCodecParser(nullptr),
      mFrame(nullptr),
      mExtraData(nullptr),
      mCodecID(aCodecID),
      mVideoCodec(IsVideoCodec(aCodecID)),
      mTaskQueue(TaskQueue::Create(
          GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
          "FFmpegDataDecoder")),
      mLastInputDts(media::TimeUnit::FromNegativeInfinity()) {
  MOZ_ASSERT(aLib);
  MOZ_COUNT_CTOR(FFmpegDataDecoder);
}

FFmpegDataDecoder<LIBAV_VER>::~FFmpegDataDecoder() {
  MOZ_COUNT_DTOR(FFmpegDataDecoder);
  if (mCodecParser) {
    mLib->av_parser_close(mCodecParser);
    mCodecParser = nullptr;
  }
}

MediaResult FFmpegDataDecoder<LIBAV_VER>::AllocateExtraData() {
  if (mExtraData) {
    mCodecContext->extradata_size = mExtraData->Length();
    // FFmpeg may use SIMD instructions to access the data which reads the
    // data in 32 bytes block. Must ensure we have enough data to read.
    uint32_t padding_size =
#if LIBAVCODEC_VERSION_MAJOR >= 58
        AV_INPUT_BUFFER_PADDING_SIZE;
#else
        FF_INPUT_BUFFER_PADDING_SIZE;
#endif
    mCodecContext->extradata = static_cast<uint8_t*>(
        mLib->av_malloc(mExtraData->Length() + padding_size));
    if (!mCodecContext->extradata) {
      return MediaResult(NS_ERROR_OUT_OF_MEMORY,
                         RESULT_DETAIL("Couldn't init ffmpeg extradata"));
    }
    memcpy(mCodecContext->extradata, mExtraData->Elements(),
           mExtraData->Length());
  } else {
    mCodecContext->extradata_size = 0;
  }

  return NS_OK;
}

// Note: This doesn't run on the ffmpeg TaskQueue, it runs on some other media
// taskqueue
MediaResult FFmpegDataDecoder<LIBAV_VER>::InitSWDecoder(
    AVDictionary** aOptions) {
  FFMPEG_LOG("Initialising FFmpeg decoder");

  AVCodec* codec = FindSoftwareAVCodec(mLib, mCodecID);
  if (!codec) {
    FFMPEG_LOG("  couldn't find ffmpeg decoder for codec id %d", mCodecID);
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("unable to find codec"));
  }
  FFMPEG_LOG("  codec %s : %s", codec->name, codec->long_name);

  StaticMutexAutoLock mon(sMutex);

  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEG_LOG("  couldn't allocate ffmpeg context for codec %s", codec->name);
    return MediaResult(NS_ERROR_OUT_OF_MEMORY,
                       RESULT_DETAIL("Couldn't init ffmpeg context"));
  }

  if (NeedParser()) {
    MOZ_ASSERT(mCodecParser == nullptr);
    mCodecParser = mLib->av_parser_init(mCodecID);
    if (mCodecParser) {
      mCodecParser->flags |= ParserFlags();
    }
  }
  mCodecContext->opaque = this;

  InitCodecContext();
  MediaResult ret = AllocateExtraData();
  if (NS_FAILED(ret)) {
    FFMPEG_LOG("  couldn't allocate ffmpeg extra data for codec %s",
               codec->name);
    mLib->av_freep(&mCodecContext);
    return ret;
  }

#if LIBAVCODEC_VERSION_MAJOR < 57
  if (codec->capabilities & CODEC_CAP_DR1) {
    mCodecContext->flags |= CODEC_FLAG_EMU_EDGE;
  }
#endif

  if (mLib->avcodec_open2(mCodecContext, codec, aOptions) < 0) {
    if (mCodecContext->extradata) {
      mLib->av_freep(&mCodecContext->extradata);
    }
    mLib->av_freep(&mCodecContext);
    FFMPEG_LOG("  Couldn't open avcodec for %s", codec->name);
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("Couldn't open avcodec"));
  }

  FFMPEG_LOG("  FFmpeg decoder init successful.");
  return NS_OK;
}

RefPtr<ShutdownPromise> FFmpegDataDecoder<LIBAV_VER>::Shutdown() {
  RefPtr<FFmpegDataDecoder<LIBAV_VER>> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    self->ProcessShutdown();
    return self->mTaskQueue->BeginShutdown();
  });
}

RefPtr<MediaDataDecoder::DecodePromise> FFmpegDataDecoder<LIBAV_VER>::Decode(
    MediaRawData* aSample) {
  return InvokeAsync<MediaRawData*>(mTaskQueue, this, __func__,
                                    &FFmpegDataDecoder::ProcessDecode, aSample);
}

RefPtr<MediaDataDecoder::DecodePromise>
FFmpegDataDecoder<LIBAV_VER>::ProcessDecode(MediaRawData* aSample) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  PROCESS_DECODE_LOG(aSample);
  bool gotFrame = false;
  DecodedData results;
  MediaResult rv = DoDecode(aSample, &gotFrame, results);
  if (NS_FAILED(rv)) {
    return DecodePromise::CreateAndReject(rv, __func__);
  }
  return DecodePromise::CreateAndResolve(std::move(results), __func__);
}

MediaResult FFmpegDataDecoder<LIBAV_VER>::DoDecode(
    MediaRawData* aSample, bool* aGotFrame,
    MediaDataDecoder::DecodedData& aResults) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());

  uint8_t* inputData = const_cast<uint8_t*>(aSample->Data());
  size_t inputSize = aSample->Size();

  mLastInputDts = aSample->mTimecode;

  if (inputData && mCodecParser) {  // inputData is null when draining.
    if (aGotFrame) {
      *aGotFrame = false;
    }
    while (inputSize) {
      uint8_t* data = inputData;
      int size = inputSize;
      int len = mLib->av_parser_parse2(
          mCodecParser, mCodecContext, &data, &size, inputData, inputSize,
          aSample->mTime.ToMicroseconds(), aSample->mTimecode.ToMicroseconds(),
          aSample->mOffset);
      if (size_t(len) > inputSize) {
        return NS_ERROR_DOM_MEDIA_DECODE_ERR;
      }
      if (size) {
        bool gotFrame = false;
        MediaResult rv = DoDecode(aSample, data, size, &gotFrame, aResults);
        if (NS_FAILED(rv)) {
          return rv;
        }
        if (gotFrame && aGotFrame) {
          *aGotFrame = true;
        }
      }
      inputData += len;
      inputSize -= len;
    }
    return NS_OK;
  }
  return DoDecode(aSample, inputData, inputSize, aGotFrame, aResults);
}

RefPtr<MediaDataDecoder::FlushPromise> FFmpegDataDecoder<LIBAV_VER>::Flush() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &FFmpegDataDecoder<LIBAV_VER>::ProcessFlush);
}

RefPtr<MediaDataDecoder::DecodePromise> FFmpegDataDecoder<LIBAV_VER>::Drain() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &FFmpegDataDecoder<LIBAV_VER>::ProcessDrain);
}

RefPtr<MediaDataDecoder::DecodePromise>
FFmpegDataDecoder<LIBAV_VER>::ProcessDrain() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  FFMPEG_LOG("FFmpegDataDecoder: draining buffers");
  RefPtr<MediaRawData> empty(new MediaRawData());
  empty->mTimecode = mLastInputDts;
  bool gotFrame = false;
  DecodedData results;
  // When draining the underlying FFmpeg decoder without encountering any
  // problems, DoDecode will either return a single frame at a time until
  // gotFrame is set to false, or it will return a block of frames with
  // NS_ERROR_DOM_MEDIA_END_OF_STREAM (EOS). However, if any issue arises, such
  // as pending data in the pipeline being corrupt or invalid, non-EOS errors
  // like NS_ERROR_DOM_MEDIA_DECODE_ERR will be returned and must be handled
  // accordingly.
  do {
    MediaResult r = DoDecode(empty, &gotFrame, results);
    if (NS_FAILED(r)) {
      if (r.Code() == NS_ERROR_DOM_MEDIA_END_OF_STREAM) {
        break;
      }
      return DecodePromise::CreateAndReject(r, __func__);
    }
  } while (gotFrame);
  return DecodePromise::CreateAndResolve(std::move(results), __func__);
}

RefPtr<MediaDataDecoder::FlushPromise>
FFmpegDataDecoder<LIBAV_VER>::ProcessFlush() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  if (mCodecContext) {
    FFMPEG_LOG("FFmpegDataDecoder: flushing buffers");
    mLib->avcodec_flush_buffers(mCodecContext);
  }
  if (mCodecParser) {
    FFMPEG_LOG("FFmpegDataDecoder: reinitializing parser");
    mLib->av_parser_close(mCodecParser);
    mCodecParser = mLib->av_parser_init(mCodecID);
  }
  return FlushPromise::CreateAndResolve(true, __func__);
}

void FFmpegDataDecoder<LIBAV_VER>::ProcessShutdown() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  StaticMutexAutoLock mon(sMutex);

  if (mCodecContext) {
    FFMPEG_LOG("FFmpegDataDecoder: shutdown");
    if (mCodecContext->extradata) {
      mLib->av_freep(&mCodecContext->extradata);
    }
#if LIBAVCODEC_VERSION_MAJOR < 57
    mLib->avcodec_close(mCodecContext);
    mLib->av_freep(&mCodecContext);
#else
    mLib->avcodec_free_context(&mCodecContext);
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 55
    mLib->av_frame_free(&mFrame);
#elif LIBAVCODEC_VERSION_MAJOR == 54
    mLib->avcodec_free_frame(&mFrame);
#else
    mLib->av_freep(&mFrame);
#endif
  }
}

AVFrame* FFmpegDataDecoder<LIBAV_VER>::PrepareFrame() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (mFrame) {
    mLib->av_frame_unref(mFrame);
  } else {
    mFrame = mLib->av_frame_alloc();
  }
#elif LIBAVCODEC_VERSION_MAJOR == 54
  if (mFrame) {
    mLib->avcodec_get_frame_defaults(mFrame);
  } else {
    mFrame = mLib->avcodec_alloc_frame();
  }
#else
  mLib->av_freep(&mFrame);
  mFrame = mLib->avcodec_alloc_frame();
#endif
  return mFrame;
}

/* static */ AVCodec* FFmpegDataDecoder<LIBAV_VER>::FindSoftwareAVCodec(
    FFmpegLibWrapper* aLib, AVCodecID aCodec) {
  MOZ_ASSERT(aLib);

  // We use this instead of MOZ_USE_HWDECODE because it is possible to disable
  // support for hardware decoding in Firefox, while the system ffmpeg library
  // still exposes the hardware codecs.
#if LIBAVCODEC_VERSION_MAJOR >= 58
  AVCodec* fallbackCodec = nullptr;
  void* opaque = nullptr;
  while (AVCodec* codec = aLib->av_codec_iterate(&opaque)) {
    if (codec->id != aCodec || !aLib->av_codec_is_decoder(codec)) {
      continue;
    }

    if (codec->capabilities & AV_CODEC_CAP_HARDWARE) {
      continue;
    }

    // We prefer to use our own OpenH264 decoder through the plugin over ffmpeg
    // by default due to broken decoding with some versions. openh264 has broken
    // decoding of some h264 videos so don't use it unless explicitly allowed
    // for now.
    if (strcmp(codec->name, "libopenh264") == 0) {
      if (!StaticPrefs::media_ffmpeg_allow_openh264()) {
        FFMPEGV_LOG("libopenh264 available but disabled by pref");
      } else if (!fallbackCodec) {
        fallbackCodec = codec;
      }
      continue;
    }

    if (codec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) {
      if (!fallbackCodec) {
        fallbackCodec = codec;
      }
      continue;
    }

    FFMPEGV_LOG("Using preferred software codec %s", codec->name);
    return codec;
  }

  if (fallbackCodec) {
    FFMPEGV_LOG("Using fallback software codec %s", fallbackCodec->name);
  }
  return fallbackCodec;
#else
  AVCodec* codec = aLib->avcodec_find_decoder(aCodec);
  if (codec) {
    // We prefer to use our own OpenH264 decoder through the plugin over ffmpeg
    // by default due to broken decoding with some versions. openh264 has broken
    // decoding of some h264 videos so don't use it unless explicitly allowed
    // for now.
    if (strcmp(codec->name, "libopenh264") == 0 &&
        !StaticPrefs::media_ffmpeg_allow_openh264()) {
      FFMPEGV_LOG("libopenh264 selected but disabled by pref");
      return nullptr;
    }

    FFMPEGV_LOG("Using preferred software codec %s", codec->name);
  }
  return codec;
#endif
}

#ifdef MOZ_USE_HWDECODE
/* static */ AVCodec* FFmpegDataDecoder<LIBAV_VER>::FindHardwareAVCodec(
    FFmpegLibWrapper* aLib, AVCodecID aCodec, AVHWDeviceType aDeviceType) {
  AVCodec* fallbackCodec = nullptr;
  void* opaque = nullptr;
  const bool ignoreDeviceType = aDeviceType == AV_HWDEVICE_TYPE_NONE;
  while (AVCodec* codec = aLib->av_codec_iterate(&opaque)) {
    if (codec->id != aCodec || !aLib->av_codec_is_decoder(codec)) {
      continue;
    }

    bool hasHwConfig =
        codec->capabilities & AV_CODEC_CAP_HARDWARE && ignoreDeviceType;
    if (!hasHwConfig) {
      for (int i = 0; const AVCodecHWConfig* config =
                          aLib->avcodec_get_hw_config(codec, i);
           ++i) {
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            (ignoreDeviceType || config->device_type == aDeviceType)) {
          hasHwConfig = true;
          break;
        }
      }
    }

    if (!hasHwConfig) {
      continue;
    }

    if (codec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) {
      if (!fallbackCodec) {
        fallbackCodec = codec;
      }
      continue;
    }

    FFMPEGV_LOG("Using preferred hardware codec %s", codec->name);
    return codec;
  }

  if (fallbackCodec) {
    FFMPEGV_LOG("Using fallback hardware codec %s", fallbackCodec->name);
  }
  return nullptr;
}
#endif

}  // namespace mozilla
