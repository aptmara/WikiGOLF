/**
 * @file DlssUpscaler.cpp
 * @brief NVIDIA DLSS Super Resolution ラッパーの実装
 */

#include "DlssUpscaler.h"
#include "../core/Logger.h"

#ifdef WIKIGOLF_DLSS
#include <cstdlib>
#include <filesystem>
#include <nvsdk_ngx_helpers.h>
#include <string>
#include <system_error>
#endif

namespace graphics {

#ifdef WIKIGOLF_DLSS

namespace {

/**
 * @brief NGXへ渡すプロジェクトID。
 * @details NVIDIAからアプリIDを発行されていない場合はGUID形式の独自IDを使う
 *          （DLSS Programming Guide 5.2.1）。
 */
constexpr const char *kNgxProjectId = "5b1f7c2e-8d4a-4e61-9f3b-2a7c6d0e41b8";
constexpr const char *kEngineVersion = "1.0";

NVSDK_NGX_PerfQuality_Value ToPerfQuality(DlssUpscaler::Quality quality) {
  switch (quality) {
  case DlssUpscaler::Quality::Performance:
    return NVSDK_NGX_PerfQuality_Value_MaxPerf;
  case DlssUpscaler::Quality::Balanced:
    return NVSDK_NGX_PerfQuality_Value_Balanced;
  case DlssUpscaler::Quality::Dlaa:
    return NVSDK_NGX_PerfQuality_Value_DLAA;
  case DlssUpscaler::Quality::Quality:
  default:
    return NVSDK_NGX_PerfQuality_Value_MaxQuality;
  }
}

/** @brief NGXのログ・一時ファイル置き場（書き込めないと初期化に失敗することがある）*/
std::wstring GetNgxDataPath() {
  std::filesystem::path base;
  if (const wchar_t *localAppData = _wgetenv(L"LOCALAPPDATA")) {
    base = localAppData;
  } else {
    base = std::filesystem::temp_directory_path();
  }
  const std::filesystem::path path = base / L"WikiGolf" / L"NGX";
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return path.wstring();
}

} // namespace

bool DlssUpscaler::IsCompiledIn() { return true; }

bool DlssUpscaler::Initialize(ID3D11Device *device) {
  Shutdown();
  if (!device) {
    return false;
  }
  m_device = device;

  const std::wstring dataPath = GetNgxDataPath();
  NVSDK_NGX_Result result = NVSDK_NGX_D3D11_Init_with_ProjectID(
      kNgxProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion,
      dataPath.c_str(), device);
  if (NVSDK_NGX_FAILED(result)) {
    LOG_INFO("DLSS", "NGX is unavailable on this system (result={:08X})",
             static_cast<uint32_t>(result));
    return false;
  }
  m_initialized = true;

  result = NVSDK_NGX_D3D11_GetCapabilityParameters(&m_parameters);
  if (NVSDK_NGX_FAILED(result) || !m_parameters) {
    // 非推奨の GetParameters は現行SDKのヘッダーから外れているため、
    // 取得できないほど古いドライバーではDLSSを使わない
    LOG_WARN("DLSS", "NGX capability parameters are unavailable (result={:08X}); "
                     "the NVIDIA driver may be too old",
             static_cast<uint32_t>(result));
    m_parameters = nullptr;
    Shutdown();
    return false;
  }
  m_parametersFromCapability = true;

  int needsUpdatedDriver = 0;
  unsigned int minDriverMajor = 0;
  unsigned int minDriverMinor = 0;
  if (NVSDK_NGX_SUCCEED(m_parameters->Get(
          NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
          &needsUpdatedDriver)) &&
      needsUpdatedDriver) {
    m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
                      &minDriverMajor);
    m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
                      &minDriverMinor);
    LOG_WARN("DLSS", "DLSS requires a newer NVIDIA driver ({}.{} or later)",
             minDriverMajor, minDriverMinor);
    return false;
  }

  int available = 0;
  if (NVSDK_NGX_FAILED(m_parameters->Get(
          NVSDK_NGX_Parameter_SuperSampling_Available, &available)) ||
      !available) {
    LOG_INFO("DLSS", "DLSS is not available on this GPU");
    return false;
  }
  int featureInitResult = 0;
  if (NVSDK_NGX_FAILED(m_parameters->Get(
          NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
          &featureInitResult)) ||
      !featureInitResult) {
    LOG_WARN("DLSS", "DLSS feature was denied for this application");
    return false;
  }

  m_supported = true;
  LOG_INFO("DLSS", "DLSS Super Resolution is available");
  return true;
}

void DlssUpscaler::Shutdown() {
  ReleaseFeature();
  if (m_parameters && m_parametersFromCapability) {
    NVSDK_NGX_D3D11_DestroyParameters(m_parameters);
  }
  m_parameters = nullptr;
  m_parametersFromCapability = false;
  if (m_initialized) {
    NVSDK_NGX_D3D11_Shutdown1(m_device);
    m_initialized = false;
  }
  m_supported = false;
  m_device = nullptr;
}

bool DlssUpscaler::QueryRenderSize(uint32_t outputWidth, uint32_t outputHeight,
                                   Quality quality, uint32_t &renderWidth,
                                   uint32_t &renderHeight) {
  if (!m_supported || outputWidth == 0 || outputHeight == 0) {
    return false;
  }
  if (quality == Quality::Dlaa) {
    renderWidth = outputWidth;
    renderHeight = outputHeight;
    return true;
  }

  unsigned int optimalWidth = 0;
  unsigned int optimalHeight = 0;
  unsigned int maxWidth = 0;
  unsigned int maxHeight = 0;
  unsigned int minWidth = 0;
  unsigned int minHeight = 0;
  float sharpness = 0.0f;
  const NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
      m_parameters, outputWidth, outputHeight, ToPerfQuality(quality),
      &optimalWidth, &optimalHeight, &maxWidth, &maxHeight, &minWidth,
      &minHeight, &sharpness);
  if (NVSDK_NGX_FAILED(result) || optimalWidth == 0 || optimalHeight == 0) {
    return false;
  }
  renderWidth = optimalWidth;
  renderHeight = optimalHeight;
  return true;
}

void DlssUpscaler::ReleaseFeature() {
  if (m_feature) {
    NVSDK_NGX_D3D11_ReleaseFeature(m_feature);
    m_feature = nullptr;
  }
  m_featureRenderWidth = 0;
  m_featureRenderHeight = 0;
  m_featureOutputWidth = 0;
  m_featureOutputHeight = 0;
}

bool DlssUpscaler::CreateFeature(ID3D11DeviceContext *context,
                                 const EvaluateParams &params) {
  ReleaseFeature();

  NVSDK_NGX_DLSS_Create_Params createParams = {};
  createParams.Feature.InWidth = params.renderWidth;
  createParams.Feature.InHeight = params.renderHeight;
  createParams.Feature.InTargetWidth = params.outputWidth;
  createParams.Feature.InTargetHeight = params.outputHeight;
  createParams.Feature.InPerfQualityValue = ToPerfQuality(params.quality);
  // シーンカラーは sRGB 相当の LDR（0～1）なので IsHDR は立てない。
  // 速度は描画解像度で、ジッターを含まない値を渡す。
  createParams.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

  const NVSDK_NGX_Result result = NGX_D3D11_CREATE_DLSS_EXT(
      context, &m_feature, m_parameters, &createParams);
  if (NVSDK_NGX_FAILED(result) || !m_feature) {
    LOG_ERROR("DLSS", "Failed to create DLSS feature {}x{} -> {}x{} (result={:08X})",
              params.renderWidth, params.renderHeight, params.outputWidth,
              params.outputHeight, static_cast<uint32_t>(result));
    m_feature = nullptr;
    return false;
  }

  m_featureRenderWidth = params.renderWidth;
  m_featureRenderHeight = params.renderHeight;
  m_featureOutputWidth = params.outputWidth;
  m_featureOutputHeight = params.outputHeight;
  m_featureQuality = params.quality;
  LOG_INFO("DLSS", "DLSS feature created {}x{} -> {}x{} quality={}",
           params.renderWidth, params.renderHeight, params.outputWidth,
           params.outputHeight, static_cast<int>(params.quality));
  return true;
}

bool DlssUpscaler::Evaluate(ID3D11DeviceContext *context,
                            const EvaluateParams &params) {
  if (!m_supported || !context || !params.color || !params.depth ||
      !params.motionVectors || !params.output) {
    return false;
  }

  bool reset = params.reset;
  const bool featureMatches =
      m_feature && m_featureRenderWidth == params.renderWidth &&
      m_featureRenderHeight == params.renderHeight &&
      m_featureOutputWidth == params.outputWidth &&
      m_featureOutputHeight == params.outputHeight &&
      m_featureQuality == params.quality;
  if (!featureMatches) {
    if (!CreateFeature(context, params)) {
      return false;
    }
    reset = true;
  }

  NVSDK_NGX_D3D11_DLSS_Eval_Params evalParams = {};
  evalParams.Feature.pInColor = params.color;
  evalParams.Feature.pInOutput = params.output;
  evalParams.pInDepth = params.depth;
  evalParams.pInMotionVectors = params.motionVectors;
  evalParams.InJitterOffsetX = params.jitterOffsetX;
  evalParams.InJitterOffsetY = params.jitterOffsetY;
  evalParams.InRenderSubrectDimensions.Width = params.renderWidth;
  evalParams.InRenderSubrectDimensions.Height = params.renderHeight;
  evalParams.InReset = reset ? 1 : 0;
  evalParams.InMVScaleX = params.motionVectorScaleX;
  evalParams.InMVScaleY = params.motionVectorScaleY;
  evalParams.InFrameTimeDeltaInMsec = params.frameTimeMs;

  const NVSDK_NGX_Result result =
      NGX_D3D11_EVALUATE_DLSS_EXT(context, m_feature, m_parameters, &evalParams);
  if (NVSDK_NGX_FAILED(result)) {
    LOG_WARN("DLSS", "DLSS evaluate failed (result={:08X})",
             static_cast<uint32_t>(result));
    return false;
  }
  return true;
}

#else // WIKIGOLF_DLSS

bool DlssUpscaler::IsCompiledIn() { return false; }

bool DlssUpscaler::Initialize(ID3D11Device *) { return false; }

void DlssUpscaler::Shutdown() {}

bool DlssUpscaler::QueryRenderSize(uint32_t, uint32_t, Quality, uint32_t &,
                                   uint32_t &) {
  return false;
}

bool DlssUpscaler::Evaluate(ID3D11DeviceContext *, const EvaluateParams &) {
  return false;
}

void DlssUpscaler::ReleaseFeature() {}

bool DlssUpscaler::CreateFeature(ID3D11DeviceContext *, const EvaluateParams &) {
  return false;
}

#endif // WIKIGOLF_DLSS

} // namespace graphics
