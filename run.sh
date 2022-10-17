#!/bin/bash
#*************************************************************************#
# @param
# input_url: video path or image url. for example: /path/to/your/video.mp4, /parth/to/your/images/%d.jpg
# how_to_show: [image] means dump as images, [video] means dump as video (output.avi), [display] means show in a window
#              default value is [display]
# output_dir: where to store the output file. default value is "./"
# output_frame_rate: output frame rate, valid when [how_to_show] set to [video] or [display]. default value is 25
# label_path: label path
# model_path: your offline model path
# func_name:  function name in your model, default value is [subnet0]
# keep_aspect_ratio: keep aspect ratio for image scaling in model's preprocessing. default value is false
# mean_value: mean values in format "100.0, 100.0, 100.0" in BGR order. default value is "0, 0, 0"
# std: std values in format "100.0, 100.0, 100.0", in BGR order. default value is "1.0, 1.0, 1.0"
# model_input_pixel_format: input image format for your model. BGRA/RGBA/ARGB/ABGR/RGB/BGR is supported. default value is BGRA
# dev_id: device odinal index. default value is 0
#*************************************************************************#

CURRENT_DIR=$(cd $(dirname ${BASH_SOURCE[0]});pwd)

mkdir -p output
mkdir -p output_mat

$CURRENT_DIR/bin/zhongyuan_server  \
  --perf_level 3 \
  --alsologtostderr
