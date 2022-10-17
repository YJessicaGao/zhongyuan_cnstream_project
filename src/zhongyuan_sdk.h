#ifndef ZHONGYUAN_SDK_H_
#define ZHONGYUAN_SDK_H_

#include <functional>
#include <vector>

#include <opencv2/opencv.hpp>
#if (CV_MAJOR_VERSION >= 4)
#include <opencv2/videoio/videoio_c.h>
#endif

// --------------------------------------------------------------------------------------------
struct camConf {
  char* rtspPath;
  char* CamID;
  int iFrameInterval;
};

struct DpResult {
  float fConf;
  float fTrackConf;
  cv::Rect rctTgt;
  int64_t iObjectId;
  int iType;
};

struct DeepLearnResult {
  char* ssCamID;
  std::string ssTime;
  int64_t iTimeStamp;
  cv::Mat imgMat;
  std::vector<DpResult> vDpResult;
  int iSrcWidth;
  int iSrcHeight;
};

using LPDeepStreamResultCallBack = std::function<void(DeepLearnResult, void*)>;
using H264SrcDataCallback = std::function<void(unsigned char*, char*, int, int64_t, void*)>;

#endif  // ZHONGYUAN_SDK_H_