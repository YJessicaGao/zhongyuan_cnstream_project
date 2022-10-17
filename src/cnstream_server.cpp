#include "cnstream_server.h"

#include <gflags/gflags.h>
#include <opencv2/opencv.hpp>
#if (CV_MAJOR_VERSION >= 4)
#include <opencv2/videoio/videoio_c.h>
#endif
#include <stdio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cnstream_frame_va.hpp"
#include "data_source.hpp"
#include "profiler/pipeline_profiler.hpp"
#include "profiler/profile.hpp"
#include "profiler/trace_serialize_helper.hpp"
#include "util/cnstream_rwlock.hpp"
#include "util/cnstream_timer.hpp"

DEFINE_int32(perf_level, 3, "perf level");
DEFINE_bool(enable_profiling, true, "enable cnstream profiling");
DEFINE_bool(enable_tracing, true, "enable cnstream tracing");
DEFINE_bool(enable_perf_print, true, "enable  printing cnstream profiling");
DEFINE_string(trace_data_dir, "", "dump trace data to specified dir. An empty string means that no data is stored");

static
std::string FindTheSlowestOne(const cnstream::PipelineProfile& profile) {
  std::string slowest_module_name = "";
  double minimum_fps = std::numeric_limits<double>::max();
  for (const auto& module_profile : profile.module_profiles) {
    for (const auto& process_profile : module_profile.process_profiles) {
      if (process_profile.process_name == cnstream::kPROCESS_PROFILER_NAME) {
        if (minimum_fps > process_profile.fps) {
          minimum_fps = process_profile.fps;
          slowest_module_name = module_profile.module_name;
        }
      }
    }
  }
  return slowest_module_name;
}

static
std::string FillStr(std::string str, uint32_t length, char charactor) {
  int filled_length = (length - str.length()) / 2;
  filled_length = filled_length > 0 ? filled_length : 0;
  int remainder = 0;
  if (filled_length && (length - str.length()) % 2) remainder = 1;
  return std::string(filled_length + remainder, charactor) + str + std::string(filled_length, charactor);
}


static
void PrintProcessPerformance(std::ostream& os, const cnstream::ProcessProfile& profile) {
  if (FLAGS_perf_level <= 1) {
    if (FLAGS_perf_level == 1) {
      os << "[Latency]: (Avg): " << profile.latency << "ms";
      os << ", (Min): " << profile.minimum_latency << "ms";
      os << ", (Max): " << profile.maximum_latency << "ms" << std::endl;
    }
    os << "[Counter]: " << profile.counter;
    os << ", [Throughput]: " << profile.fps << "fps" << std::endl;
  } else if (FLAGS_perf_level >=2) {
    os << "[Counter]: " << profile.counter;
    os << ", [Completed]: " << profile.completed;
    os << ", [Dropped]: " << profile.dropped;
    os << ", [Ongoing]: " << profile.ongoing << std::endl;
    os << "[Latency]: (Avg): " << profile.latency << "ms";
    os << ", (Min): " << profile.minimum_latency << "ms";
    os << ", (Max): " << profile.maximum_latency << "ms" << std::endl;
    os << "[Throughput]: " << profile.fps << "fps" << std::endl;
  }

  if (FLAGS_perf_level >= 3) {
    uint32_t stream_name_max_length = 15;
    if (profile.stream_profiles.size()) {
      os << "\n------ Stream ------\n";
    }
    for (const auto& stream_profile : profile.stream_profiles) {
      std::string stream_name = "[" + stream_profile.stream_name + "]";
      os << stream_name << std::string(stream_name_max_length - stream_name.length(), ' ');
      os << "[Counter]: " << stream_profile.counter;
      os << ", [Completed]: " << stream_profile.completed;
      os << ", [Dropped]: " << stream_profile.dropped << std::endl;
      os << std::string(stream_name_max_length, ' ');
      os << "[Latency]: (Avg): " << stream_profile.latency << "ms";
      os << ", (Min): " << stream_profile.minimum_latency << "ms";
      os << ", (Max): " << stream_profile.maximum_latency << "ms" << std::endl;
      os << std::string(stream_name_max_length, ' ');
      os << "[Throughput]: " << stream_profile.fps << "fps" << std::endl;
    }
  }
}

void PrintPipelinePerformance(const std::string& prefix_str, const cnstream::PipelineProfile& profile) {
  auto slowest_module_name = FindTheSlowestOne(profile);
  std::stringstream ss;
  int length = 80;
  ss << "\033[1m\033[36m" << FillStr("  Performance Print Start  (" + prefix_str + ")  ", length, '*') << "\033[0m\n";
  ss << "\033[1m" << FillStr("  Pipeline: [" + profile.pipeline_name + "]  ", length, '=') << "\033[0m\n";

  for (const auto& module_profile : profile.module_profiles) {
    ss << "\033[1m\033[32m" << FillStr(" Module: [" + module_profile.module_name + "] ", length, '-');
    if (slowest_module_name == module_profile.module_name) {
      ss << "\033[0m\033[41m" << " (slowest) ";
    }
    ss << "\033[0m\n";

    for (const auto& process_profile : module_profile.process_profiles) {
      ss << "\033[1m\033[33m" << std::string(length / 8, '-');
      ss << "Process Name: [" << process_profile.process_name << "\033[0m" << "]\n";
      PrintProcessPerformance(ss, process_profile);
    }
  }
  ss << "\n\033[1m\033[32m" << FillStr("  Overall  ", length, '-') << "\033[0m\n";
  PrintProcessPerformance(ss, profile.overall_profile);
  ss << "\033[1m\033[36m" << FillStr("  Performance Print End  (" + prefix_str + ")  ", length, '*') << "\033[0m\n";
  std::cout << ss.str() << std::endl;
}

// --------------------------------------------------------------------------------------------
class CnsPipeline : public cnstream::Pipeline, public cnstream::StreamMsgObserver, cnstream::IModuleObserver {
 public:
  CnsPipeline(const std::string& name, int dev_id) : cnstream::Pipeline(name) {
    SetStreamMsgObserver(this);
    dev_id_ = dev_id;
  }
  int Init();
  void Destroy();
  int AddSource(const std::string& url, const std::string& stream_id, int interval);
  int RemoveSource(const std::string& stream_id, bool force = false);
  void SetInferCallback(LPDeepStreamResultCallBack cb) { infer_cb_ = cb; }
  void SetStreamCallback(H264SrcDataCallback cb) { stream_cb_ = cb; }

 private:
  void notify(cnstream::CNFrameInfoPtr frame_info) override;
  void Update(const cnstream::StreamMsg& msg) override;

 private:
  int CnsBuildPipeline();
  void RtspHandlerCallback(cnstream::ESPacket pkg, std::string stream_id);
  void FrameDoneCallback(cnstream::CNFrameInfoPtr);

 private:
  int dev_id_ = 0;
  cnstream::DataSource *source_ = nullptr;
  std::atomic<bool> stop_{false};
  std::set<std::string> stream_set_;
  std::condition_variable wakener_;
  mutable std::mutex mutex_;
  LPDeepStreamResultCallBack infer_cb_ = nullptr;
  H264SrcDataCallback stream_cb_  = nullptr;

  std::future<void> perf_print_th_ret_;
  std::atomic<bool> stop_perf_print_{false};
  int trace_data_file_cnt_ = 0;
};

void CnsPipeline::Update(const cnstream::StreamMsg& msg) {
  std::unique_lock<std::mutex> lk(mutex_);
  switch (msg.type) {
    case cnstream::StreamMsgType::EOS_MSG:
      if (stream_set_.find(msg.stream_id) != stream_set_.end()) {
        if (source_) {
          source_->RemoveSource(msg.stream_id);
        }
        stream_set_.erase(msg.stream_id);
      }
      if (stream_set_.empty()) {
        LOGI(CnsPipeline) << "[" << GetName() << "] received all EOS";
        stop_ = true;
      }
      break;
    case cnstream::StreamMsgType::FRAME_ERR_MSG:
      LOGW(CnsPipeline) << "[" << GetName() << "] received frame error from stream: " << msg.stream_id
                          << ", pts: " << msg.pts << ".";
      break;
    default:
      LOGE(CnsPipeline) << "[" << GetName() << "] receive unhandled message, type["
                        << static_cast<int>(msg.type) << "], remove source [" << msg.stream_id << "]";
      if (stream_set_.find(msg.stream_id) != stream_set_.end()) {
        if (source_) {
          source_->RemoveSource(msg.stream_id, true);
        }
        stream_set_.erase(msg.stream_id);
      }
      if (stream_set_.empty()) {
        LOGI(CnsPipeline) << "[" << GetName() << "] all streams is removed from pipeline, pipeline will stop.";
        stop_ = true;
      }
      break;
  }
  if (stop_) {
    wakener_.notify_one();
  }
}

int CnsPipeline::CnsBuildPipeline() {
  std::string dev_id_str = std::to_string(dev_id_);

  cnstream::ProfilerConfig profiler_config;
  profiler_config.enable_profiling = FLAGS_enable_profiling;
  profiler_config.enable_tracing = FLAGS_enable_tracing;

  std::vector<cnstream::CNModuleConfig> module_configs;
  cnstream::CNModuleConfig source_config;
  source_config.name = "source";
  source_config.parallelism = 0;
  source_config.className = "cnstream::DataSource";
  source_config.next = {"detector"};
  source_config.parameters = {
    std::make_pair("reuse_cndec_buf", "true"),
    std::make_pair("output_type", "mlu"),
    std::make_pair("decoder_type", "mlu"),
    std::make_pair("input_buf_number", "6"),
    std::make_pair("output_buf_number", "16"),
    std::make_pair("device_id", dev_id_str)
  };
  module_configs.push_back(source_config);

  cnstream::CNModuleConfig detector_config;
  detector_config.name = "detector";
  detector_config.parallelism = 1;
  detector_config.maxInputQueueSize = 10;
  detector_config.className = "cnstream::Inferencer2";
  detector_config.next = {"tacker"};
  detector_config.parameters = {
    std::make_pair("model_path", "offline-models/yolov5_b4c4_rgb_mlu270.cambricon"),
    // std::make_pair("preproc_name", "PreprocYolov5S"),
    std::make_pair("preproc_name", "CNCV"),
    std::make_pair("keep_aspect_ratio", "true"),
    std::make_pair("normalize", "true"),
    std::make_pair("model_input_pixel_format", "RGB24"),
    std::make_pair("postproc_name", "VideoPostprocYolov5"),
    std::make_pair("threshold", "0.6"),
    std::make_pair("engine_num", "4"),
    std::make_pair("device_id", dev_id_str)
  };
  module_configs.push_back(detector_config);

  cnstream::CNModuleConfig tracker_config;
  tracker_config.name = "tacker";
  tracker_config.parallelism = 4;
  tracker_config.maxInputQueueSize = 10;
  tracker_config.className = "cnstream::Tracker";
  tracker_config.next = {"osd"};
  tracker_config.parameters = {
    std::make_pair("model_path", "offline-models/feature_extract_for_tracker_b4c4_argb_mlu270.cambricon"),
    std::make_pair("max_cosine_distance", "0.06"),
    std::make_pair("device_id", dev_id_str)
  };
  module_configs.push_back(tracker_config);

  cnstream::CNModuleConfig osd_config;
  osd_config.name = "osd";
  osd_config.parallelism = 4;
  osd_config.className = "cnstream::Osd";
  osd_config.maxInputQueueSize = 10;
  osd_config.next = {"encode"};
  osd_config.parameters = {
    std::make_pair("label_path", "labels/label_map_coco.txt")
  };
  module_configs.push_back(osd_config);

  cnstream::CNModuleConfig encode_config;
  encode_config.name = "encode";
  encode_config.parallelism = 4;
  encode_config.maxInputQueueSize = 10;
  encode_config.className = "cnstream::Encode";
  encode_config.parameters = {
    std::make_pair("file_name", "output/output.jpg"),
    std::make_pair("device_id", dev_id_str)
  };
  module_configs.push_back(encode_config);

  if (BuildPipeline(module_configs, profiler_config)) {
    return 0;
  }
  return -1;
}

int CnsPipeline::Init() {
  if (CnsBuildPipeline() != 0) {
    LOGE(CnsPipeline) << "Build pipeline failed.";
    return -1;
  }
  if (!Start()) {
    LOGE(CnsPipeline) << "Start pipeline failed.";
    Destroy();
    return -1;
  }
  source_ = dynamic_cast<cnstream::DataSource*>(GetModule("source"));
  if (!source_) {
    LOGE(CnsPipeline) << "Can not find a data source from pipeline, check the config file please.";
    Destroy();
    return -1;
  }
  RegisterFrameDoneCallBack(std::bind(&CnsPipeline::FrameDoneCallback, this, std::placeholders::_1));
  stop_ = false;
  stop_perf_print_ = false;
  trace_data_file_cnt_ = 0;

  // profiling and tracing
  if (FLAGS_enable_perf_print) {
    if (IsProfilingEnabled()) {
      perf_print_th_ret_ = std::async(std::launch::async, [this] {
        cnstream::Time last_time = cnstream::Clock::now();
        int trace_data_dump_times = 0;
        cnstream::TraceSerializeHelper trace_dumper;
        while (!stop_perf_print_) {
          std::this_thread::sleep_for(std::chrono::seconds(2));
          if (stop_perf_print_) break;
          ::PrintPipelinePerformance("Whole", GetProfiler()->GetProfile());
          if (IsTracingEnabled()) {
            cnstream::Duration duration(2000);
            ::PrintPipelinePerformance("Last two seconds",
                                      GetProfiler()->GetProfileBefore(cnstream::Clock::now(), duration));
            if (!FLAGS_trace_data_dir.empty()) {
              cnstream::Time now_time = cnstream::Clock::now();
              trace_dumper.Serialize(GetTracer()->GetTrace(last_time, now_time));
              last_time = now_time;
              if (++trace_data_dump_times == 10) {
                trace_dumper.ToFile(FLAGS_trace_data_dir + "/cnstream_trace_data_" +
                                    std::to_string(trace_data_file_cnt_++));
                trace_dumper.Reset();
                trace_data_dump_times = 0;
              }
            }
          }
        }
        if (IsTracingEnabled() && !FLAGS_trace_data_dir.empty() && trace_data_dump_times) {
          trace_dumper.ToFile(FLAGS_trace_data_dir + "/cnstream_trace_data_" + std::to_string(trace_data_file_cnt_++));
          trace_dumper.Reset();
        }
      });
    }
  }

  return 0;
}

void CnsPipeline::Destroy() {
  std::unique_lock<std::mutex> lk(mutex_);
  if (source_) {
    for (auto it : stream_set_) {
      source_->RemoveSource(it);
    }
  }
  if (stream_set_.empty()) {
    stop_ = true;
  }
  wakener_.wait(lk, [this]() { return stop_.load(); });
  lk.unlock();
  Stop();
  if (IsProfilingEnabled()) {
    stop_perf_print_ = true;
    perf_print_th_ret_.get();
    ::PrintPipelinePerformance("Whole", GetProfiler()->GetProfile());
  }
  if (IsTracingEnabled() && !FLAGS_trace_data_dir.empty()) {
    LOGI(CnsPipeline) << "Wait for trace data merge ...";
    cnstream::TraceSerializeHelper helper;
    for (int file_index = 0; file_index < trace_data_file_cnt_; ++file_index) {
      std::string filename = FLAGS_trace_data_dir + "/cnstream_trace_data_" + std::to_string(file_index);
      cnstream::TraceSerializeHelper t;
      cnstream::TraceSerializeHelper::DeserializeFromJSONFile(filename, &t);
      helper.Merge(t);
      remove(filename.c_str());
    }
    if (!helper.ToFile(FLAGS_trace_data_dir + "/cnstream_trace_data.json")) {
      LOGE(CnsPipeline) << "Dump trace data failed.";
    }
  }

  infer_cb_ = nullptr;
  stream_cb_ = nullptr;
}

int CnsPipeline::AddSource(const std::string& url, const std::string& stream_id, int interval) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (stream_set_.find(stream_id) != stream_set_.end()) {
    LOGE(CnsPipeline) << "The stream_id [" << stream_id << "] exists, please remove first.";
    return -1;
  }

  LOGE(GYJ) << url;
  if (url.size() > 4 && "rtsp" == url.substr(0, 4)) {
    if (source_->AddSource(
        cnstream::RtspHandler::Create(source_, stream_id, url, false, -1, {},
            std::bind(&CnsPipeline::RtspHandlerCallback, this, std::placeholders::_1, std::placeholders::_2),
            interval))) {
      LOGE(CnsPipeline) << "Create data handler failed.";
      return -1;
    }
  } else {
    if (source_->AddSource(cnstream::FileHandler::Create(source_, stream_id, url, -1, false))) {
      LOGE(CnsPipeline) << "Create data handler failed.";
      return -1;
    }
  }
  stream_set_.insert(stream_id);
  return 0;
}

int CnsPipeline::RemoveSource(const std::string& stream_id, bool force) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (stream_set_.find(stream_id) != stream_set_.end()) {
    if (source_) {
      source_->RemoveSource(stream_id, force);
    }
  }
  return 0;
}

void CnsPipeline::RtspHandlerCallback(cnstream::ESPacket pkg, std::string stream_id) {
  // LOGI(CnsPipeline) << "RtspHandlerCallback";
  if (stream_cb_) {
    stream_cb_(pkg.data, const_cast<char*>(stream_id.c_str()), pkg.size, pkg.pts, nullptr);  // FIXME userdata?
  }
}

void CnsPipeline::notify(cnstream::CNFrameInfoPtr frame_info) {
  // LOGI(CnsPipeline) << "IModuleObserver::notify";
}

void CnsPipeline::FrameDoneCallback(cnstream::CNFrameInfoPtr data) {
  // LOGI(CnsPipeline) << "FrameDoneCallback";
  if (data->IsEos()) {
    return;  // FIXME
  }
  if (infer_cb_) {
    DeepLearnResult result;
    result.ssCamID = const_cast<char*>(data->stream_id.c_str());
    result.ssTime = cnstream::TimeStamp::CurrentToDate();
    result.iTimeStamp = static_cast<int64_t>(cnstream::TimeStamp::Current());  // FIXME current or frame timestamp?
    cnstream::CNDataFramePtr frame  = data->collection.Get<cnstream::CNDataFramePtr>(cnstream::kCNDataFrameTag);
    result.imgMat = frame->ImageBGR();
    result.iSrcWidth = frame->width;
    result.iSrcHeight = frame->height;

    cnstream::CNInferObjsPtr objs = data->collection.Get<cnstream::CNInferObjsPtr>(cnstream::kCNInferObjsTag);
    std::unique_lock<std::mutex> lg(objs->mutex_);
    result.vDpResult.clear();
    for (auto obj : objs->objs_) {
      DpResult dp_result;
      dp_result.fConf = obj->score;
      dp_result.fTrackConf = 0;  // FIXME track confidence is not provided in Tracker module
      // top left (x,y) and w, h
      dp_result.rctTgt = cv::Rect(obj->bbox.x * frame->width, obj->bbox.y * frame->height,
                                  obj->bbox.w * frame->width, obj->bbox.h * frame->height);
      dp_result.iObjectId = std::stoll(obj->track_id);
      dp_result.iType = std::stoi(obj->id);
      result.vDpResult.push_back(dp_result);
    }
    infer_cb_(result, nullptr);  // FIXME userdata?
  }
}

// ------------------------------- CNStreamServer -----------------------------------

CNStreamServer* CNStreamServer::GetInstance() {
  static CNStreamServer instance;
  return &instance;
}

int CNStreamServer::Open(const std::string& name, int dev_id,
                         LPDeepStreamResultCallBack infer_cb,
                         H264SrcDataCallback stream_cb) {
  cnstream::RwLockWriteGuard lk_guard(lk_);
  int key = GetKey();
  if (key == -1) {
    std::cerr << "CNStreamServer: GetKey failed";
    return key;
  }
  std::shared_ptr<CnsPipeline> pipe = std::make_shared<CnsPipeline>(name, dev_id);
  pipe->Init();
  pipe->SetInferCallback(infer_cb);
  pipe->SetStreamCallback(stream_cb);
  pipeline_map_[key] = pipe;

  return key;
}

void CNStreamServer::Close(int pipe_key) {
  cnstream::RwLockWriteGuard lk_guard(lk_);
  ReturnKey(pipe_key);
  if (pipeline_map_.find(pipe_key) != pipeline_map_.end()) {
    pipeline_map_[pipe_key]->Destroy();
    pipeline_map_.erase(pipe_key);
  }
}

int CNStreamServer::AddStream(int pipe_key, const std::string& url,
                              const std::string& stream_id, int interval) {
  cnstream::RwLockReadGuard lk_guard(lk_);
  if (pipeline_map_.find(pipe_key) != pipeline_map_.end()) {
    return pipeline_map_[pipe_key]->AddSource(url, stream_id, interval);
  }
  return -1;
}

int CNStreamServer::RemoveStream(int pipe_key, const std::string& stream_id, bool force) {
  cnstream::RwLockReadGuard lk_guard(lk_);
  if (pipeline_map_.find(pipe_key) != pipeline_map_.end()) {
    return pipeline_map_[pipe_key]->RemoveSource(stream_id, force);
  }
  return -1;
}

void CNStreamServer::Init() {
  pipe_bit_mask_ = 0;
  pipe_cnt_ = 0;
}

void CNStreamServer::Destroy() {
  pipe_bit_mask_ = 0;
  pipe_cnt_ = 0;
  for (auto it : pipeline_map_) {
    it.second->Destroy();
  }
  pipeline_map_.clear();
}
