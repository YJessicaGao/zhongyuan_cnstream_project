#include "zhongyuan_sdk.h"

#include <unistd.h> 
#include <iostream> 

#include <gflags/gflags.h>

#include "cnstream_server.h"

// -------------------------------------
void Init() {
  CNStreamServer::GetInstance()->Init();
}

void Destroy() {
  CNStreamServer::GetInstance()->Destroy();
}

int Open(const std::string& name, int dev_id,
         LPDeepStreamResultCallBack infer_cb,
         H264SrcDataCallback stream_cb) {
  return CNStreamServer::GetInstance()->Open(name, dev_id, infer_cb, stream_cb);
}

void Close(int pipe_key) {
  CNStreamServer::GetInstance()->Close(pipe_key);
}

int AddStreams(int pipe_key, std::vector<camConf> conf_vec) {
  int result = 0;
  for (auto &conf : conf_vec) {
    int ret = CNStreamServer::GetInstance()->AddStream(pipe_key, conf.rtspPath, conf.CamID, conf.iFrameInterval);
    if (ret != 0) result = -1;
  }
  return result;
}

int RemoveStreams(int pipe_key, std::vector<camConf> conf_vec) {
  int result = 0;
  for (auto &conf : conf_vec) {
    int ret = CNStreamServer::GetInstance()->RemoveStream(pipe_key, conf.CamID);
    if (ret != 0) result = -1;
  }
  return result;
}

void InferCallback(DeepLearnResult result, void* userdata) {
#if 0
  printf("\n------------------------------------------------------------------\n");
  printf("ssCamID = %s\nssTime = %s, iTimeStamp = %ld\niSrcWidth = %d, iSrcHeight = %d\nobjs_num = %lu\n",
      result.ssCamID, result.ssTime.c_str(), result.iTimeStamp, result.iSrcWidth, result.iSrcHeight, result.vDpResult.size());
  for (auto it : result.vDpResult) {
    printf("    iType = %d, fConf = %f, iObjectId = %ld, x = %d, y = %d, w = %d, h = %d\n",
           it.iType, it.fConf, it.iObjectId, it.rctTgt.x, it.rctTgt.y, it.rctTgt.width, it.rctTgt.height);
  }
  static int i = 0;
  cv::imwrite("output_mat/"+ std::to_string(i++) + ".jpg", result.imgMat);
#endif
}

void StreamCallback(unsigned char* data, char* cam_id, int length, int64_t timestamp, void* userdata) {
#if 0
  printf("\n------------------------------------------------------------------\n");
  printf("cam_id = %s\ndata = %p, length = %d\ntimestamp=%ld\n", cam_id, data, length, timestamp);
#endif
}

// -------------------------------------
int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, false);

  Init();

  int pipe_key = Open("my_pipe", 0, InferCallback, StreamCallback);

  std::vector<camConf> conf_vec;
  std::vector<std::string> stream_vec;
  std::vector<std::string> stream_name;
  int stream_num = 1;
  for (int i = 0; i < stream_num; i++) {
    std::string stream_id = "stream_" + std::to_string(i);
    std::string filename = "videos/cars.mp4";
    // std::string filename = "rtsp://admin:hello123@10.100.202.30:554/cam/realmonitor?channel=1&subtype=0";
    stream_vec.push_back(stream_id);
    stream_name.push_back(filename);
  }
  for (int i = 0; i < stream_num; i++) {
    camConf cam_conf;
    cam_conf.CamID = const_cast<char*>(stream_vec[i].c_str());
    cam_conf.rtspPath = const_cast<char*>(stream_name[i].c_str());
    cam_conf.iFrameInterval = 1;
    conf_vec.push_back(cam_conf);
  }

  AddStreams(pipe_key, conf_vec);

  usleep(10e6);

  RemoveStreams(pipe_key, conf_vec);

  Close(pipe_key);

  Destroy();
}
