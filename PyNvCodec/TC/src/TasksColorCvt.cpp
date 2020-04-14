/*
 * Copyright 2019 NVIDIA Corporation
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CodecsSupport.hpp"
#include "MemoryInterfaces.hpp"
#include "Tasks.hpp"

#include <nppi_color_conversion.h>
#include <nppi_data_exchange_and_initialization.h>
#include <stdexcept>

using namespace VPF;
using namespace std;

constexpr auto TASK_EXEC_SUCCESS = TaskExecStatus::TASK_EXEC_SUCCESS;
constexpr auto TASK_EXEC_FAIL = TaskExecStatus::TASK_EXEC_FAIL;

namespace VPF {

struct NppConvertSurface_Impl {
  virtual ~NppConvertSurface_Impl() = default;
  virtual Token *Execute(Token *pInput) = 0;
};

struct nv12_rgb final : public NppConvertSurface_Impl {
  nv12_rgb(uint32_t width, uint32_t height, CUcontext context,
           CUstream stream) {
    pSurface = Surface::Make(RGB, width, height);
  }

  ~nv12_rgb() { delete pSurface; }

  Token *Execute(Token *pInputNV12) {
    if (!pInputNV12) {
      return nullptr;
    }

    auto pInput = (SurfaceNV12 *)pInputNV12;

    auto diffW = (pInput->Width() != pSurface->Width());
    auto diffH = (pInput->Height() != pSurface->Height());
    if (diffW || diffH) {
      cerr << "Input surfaces have different size" << endl;
      return nullptr;
    }

    const Npp8u *const pSrc[] = {(const Npp8u *const)pInput->PlanePtr(0U),
                                 (const Npp8u *const)pInput->PlanePtr(1U)};

    auto pDst = (Npp8u *)pSurface->PlanePtr();
    NppiSize oSizeRoi = {(int)pInput->Width(), (int)pInput->Height()};
    auto err = nppiNV12ToRGB_709HDTV_8u_P2C3R(pSrc, pInput->Pitch(), pDst,
                                              pSurface->Pitch(), oSizeRoi);

    if (NPP_NO_ERROR != err) {
      return nullptr;
    }

    return pSurface;
  }

  Surface *pSurface = nullptr;
};

struct nv12_yuv420 final : public NppConvertSurface_Impl {
  nv12_yuv420(uint32_t width, uint32_t height, CUcontext context,
              CUstream stream) {
    pSurface = Surface::Make(YUV420, width, height);
  }

  ~nv12_yuv420() { delete pSurface; }

  Token *Execute(Token *pInputNV12) {
    if (!pInputNV12) {
      return nullptr;
    }

    auto pInput_NV12 = (SurfaceNV12 *)pInputNV12;

    auto diffW = (pInput_NV12->Width() != pSurface->Width());
    auto diffH = (pInput_NV12->Height() != pSurface->Height());
    if (diffW || diffH) {
      cerr << "Input surfaces have different size" << endl;
      return nullptr;
    }

    const Npp8u *const pSrc[] = {(const Npp8u *)pInput_NV12->PlanePtr(0U),
                                 (const Npp8u *)pInput_NV12->PlanePtr(1U)};

    Npp8u *pDst[] = {(Npp8u *)pSurface->PlanePtr(0U),
                     (Npp8u *)pSurface->PlanePtr(1U),
                     (Npp8u *)pSurface->PlanePtr(2U)};

    int dstStep[] = {(int)pSurface->Pitch(0U), (int)pSurface->Pitch(1U),
                     (int)pSurface->Pitch(2U)};
    NppiSize roi = {(int)pInput_NV12->Width(), (int)pInput_NV12->Height()};

    auto err =
        nppiYCbCr420_8u_P2P3R(pSrc[0], pInput_NV12->Pitch(0U), pSrc[1],
                              pInput_NV12->Pitch(1U), pDst, dstStep, roi);
    if (NPP_NO_ERROR != err) {
      return nullptr;
    }

    return pSurface;
  }

  Surface *pSurface = nullptr;
};

struct yuv420_nv12 final : public NppConvertSurface_Impl {
  yuv420_nv12(uint32_t width, uint32_t height, CUcontext context,
              CUstream stream) {
    pSurface = Surface::Make(NV12, width, height);
  }

  ~yuv420_nv12() { delete pSurface; }

  Token *Execute(Token *pInputYUV420) {
    if (!pInputYUV420) {
      return nullptr;
    }

    auto pInput_YUV420 = (SurfaceYUV420 *)pInputYUV420;
    auto diffW = (pInput_YUV420->Width() != pSurface->Width());
    auto diffH = (pInput_YUV420->Height() != pSurface->Height());
    if (diffW || diffH) {
      cerr << "Input surfaces have different size" << endl;
      return nullptr;
    }

    const Npp8u *const pSrc[] = {(const Npp8u *)pInput_YUV420->PlanePtr(0U),
                                 (const Npp8u *)pInput_YUV420->PlanePtr(1U),
                                 (const Npp8u *)pInput_YUV420->PlanePtr(2U)};

    Npp8u *pDst[] = {(Npp8u *)pSurface->PlanePtr(0U),
                     (Npp8u *)pSurface->PlanePtr(1U)};

    int srcStep[] = {(int)pInput_YUV420->Pitch(0U),
                     (int)pInput_YUV420->Pitch(1U),
                     (int)pInput_YUV420->Pitch(2U)};
    int dstStep[] = {(int)pSurface->Pitch(0U), (int)pSurface->Pitch(1U)};
    NppiSize roi = {(int)pInput_YUV420->Width(), (int)pInput_YUV420->Height()};

    auto err = nppiYCbCr420_8u_P3P2R(pSrc, srcStep, pDst[0], dstStep[0],
                                     pDst[1], dstStep[1], roi);
    if (NPP_NO_ERROR != err) {
      return nullptr;
    }

    return pSurface;
  }

  Surface *pSurface = nullptr;
};

struct rgb8_deinterleave final : public NppConvertSurface_Impl {
  Surface *pSurface = nullptr;

  rgb8_deinterleave(uint32_t width, uint32_t height, CUcontext context,
                    CUstream stream) {
    pSurface = Surface::Make(RGB_PLANAR, width, height);
  }

  ~rgb8_deinterleave() { delete pSurface; }

  Token *Execute(Token *pInput) {
    auto pInputRGB8 = (SurfaceRGB *)pInput;

    if (RGB != pInputRGB8->PixelFormat()) {
      return nullptr;
    }

    const Npp8u *pSrc = (const Npp8u *)pInputRGB8->PlanePtr();
    int nSrcStep = pInputRGB8->Pitch();
    Npp8u *aDst[] = {(Npp8u *)pSurface->PlanePtr(),
                     (Npp8u *)pSurface->PlanePtr() +
                         pSurface->Height() * pSurface->Pitch(),
                     (Npp8u *)pSurface->PlanePtr() +
                         pSurface->Height() * pSurface->Pitch() * 2};
    int nDstStep = pSurface->Pitch();
    NppiSize oSizeRoi = {0};
    oSizeRoi.height = pSurface->Height();
    oSizeRoi.width = pSurface->Width();

    if (NPP_NO_ERROR !=
        nppiCopy_8u_C3P3R(pSrc, nSrcStep, aDst, nDstStep, oSizeRoi)) {
      return nullptr;
    }

    return pSurface;
  }
};

} // namespace VPF

ConvertSurface::ConvertSurface(uint32_t width, uint32_t height,
                               Pixel_Format inFormat, Pixel_Format outFormat,
                               CUcontext ctx, CUstream str)
    : Task("NppConvertSurface", ConvertSurface::numInputs,
           ConvertSurface::numOutputs) {
  if (NV12 == inFormat && YUV420 == outFormat) {
    pImpl = new nv12_yuv420(width, height, ctx, str);
  } else if (YUV420 == inFormat && NV12 == outFormat) {
    pImpl = new yuv420_nv12(width, height, ctx, str);
  } else if (NV12 == inFormat && RGB == outFormat) {
    pImpl = new nv12_rgb(width, height, ctx, str);
  } else if (RGB == inFormat && RGB_PLANAR == outFormat) {
    pImpl = new rgb8_deinterleave(width, height, ctx, str);
  } else {
    stringstream ss;
    ss << "Unsupported pixel format conversion: " << inFormat << " to "
       << outFormat;
    throw invalid_argument(ss.str());
  }
}

ConvertSurface::~ConvertSurface() { delete pImpl; }

ConvertSurface *ConvertSurface::Make(uint32_t width, uint32_t height,
                                     Pixel_Format inFormat,
                                     Pixel_Format outFormat, CUcontext ctx,
                                     CUstream str) {
  return new ConvertSurface(width, height, inFormat, outFormat, ctx, str);
}

TaskExecStatus ConvertSurface::Execute() {
  ClearOutputs();
  auto pOutput = pImpl->Execute(GetInput(0));
  SetOutput(pOutput, 0U);
  return TASK_EXEC_SUCCESS;
}