#ifndef CNSTREAM_SERVER_H_
#define CNSTREAM_SERVER_H_

#include <stdio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "util/cnstream_rwlock.hpp"

#include "zhongyuan_sdk.h"

// --------------------------------------------------------------------------------------------
class CnsPipeline;
class CNStreamServer {
 public:
  static CNStreamServer* GetInstance();
  void Init();
  void Destroy();
  int Open(const std::string& name, int dev_id,
           LPDeepStreamResultCallBack infer_cb,
           H264SrcDataCallback stream_cb);
  void Close(int pipe_key);
  int AddStream(int pipe_key, const std::string& url,
                                const std::string& stream_id, int interval);
  int RemoveStream(int pipe_key, const std::string& stream_id, bool force = false);

 private:
  static constexpr int MAXIMUM_PIPE_CNT = sizeof(uint64_t) * 8;
  CNStreamServer() { };
  int GetKey() {
    int key = -1;
    for (int bit_pos = 0; bit_pos < MAXIMUM_PIPE_CNT; ++bit_pos) {
      key = bit_pos;
      if (((uint64_t)1 << key) & pipe_bit_mask_) continue;
      pipe_bit_mask_ |= ((uint32_t)1 << key);
      break;
    }
    if (key != -1) {
      pipe_cnt_++;
    }
    return key;
  }
  void ReturnKey(int key) {
    if (pipeline_map_.find(key) == pipeline_map_.end()) return;
    pipe_bit_mask_ &= ~((uint32_t)1 << key);
    pipe_cnt_--;
  }

 private:
  cnstream::RwLock lk_;
  std::unordered_map<int, std::shared_ptr<CnsPipeline>> pipeline_map_;
  uint64_t pipe_bit_mask_ = 0;
  uint32_t pipe_cnt_ = 0;
};

#endif  // CNSTREAM_SERVER_H_