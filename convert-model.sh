#!/bin/bash

set -e

model_name=yolo11s
file_name=detect
input_w=320
input_h=320

# mean: 0, 0, 0
# std: 255, 255, 255

# mean
# 1/std

# mean: 0, 0, 0
# scale: 0.00392156862745098, 0.00392156862745098, 0.00392156862745098

mkdir -p workspace
cd workspace

# convert to mlir
model_transform \
  --model_name ${model_name} \
  --model_def ../${file_name}.onnx \
  --input_shapes [[1,3,${input_h},${input_w}]] \
  --mean "0.0,0.0,0.0" \
  --scale "0.00392156862745098,0.00392156862745098,0.00392156862745098" \
  --keep_aspect_ratio \
  --pixel_format rgb \
  --channel_format nchw \
  --output_names "/model.23/cv2.0/cv2.0.2/Conv_output_0,/model.23/cv3.0/cv3.0.2/Conv_output_0,/model.23/cv2.1/cv2.1.2/Conv_output_0,/model.23/cv3.1/cv3.1.2/Conv_output_0,/model.23/cv2.2/cv2.2.2/Conv_output_0,/model.23/cv3.2/cv3.2.2/Conv_output_0" \
  --test_input ../test.jpg \
  --test_result ${net_name}_top_outputs.npz \
  --tolerance 0.99,0.99 \
  --mlir ${file_name}.mlir


# export bf16 model
#   not use --quant_input, use float32 for easy coding
# model_deploy.py \
# --mlir ${net_name}.mlir \
# --quantize BF16 \
# --processor cv181x \
# --test_input ${net_name}_in_f32.npz \
# --test_reference ${net_name}_top_outputs.npz \
# --model ${net_name}_bf16.cvimodel


model_deploy \
  --mlir ${file_name}.mlir \
  --quant_input \
  --quantize F16 \
  --customization_format RGB_PACKED \
  --processor cv181x \
  --test_input ${file_name}_in_f32.npz \
  --test_reference ${file_name}_top_outputs.npz \
  --fuse_preprocess \
  --tolerance 0.99,0.9 \
  --model ${file_name}_f16.cvimodel



# echo "calibrate for int8 model"
# # export int8 model
# run_calibration.py ${net_name}.mlir \
# --dataset ../images \
# --input_num 200 \
# -o ${net_name}_cali_table

# echo "convert to int8 model"
# # export int8 model
# #    add --quant_input, use int8 for faster processing in maix.nn.NN.forward_image
# model_deploy.py \
# --mlir ${net_name}.mlir \
# --quantize INT8 \
# --quant_input \
# --calibration_table ${net_name}_cali_table \
# --processor cv181x \
# --test_input ${net_name}_in_f32.npz \
# --test_reference ${net_name}_top_outputs.npz \
# --tolerance 0.9,0.6 \
# --model ${net_name}_int8.cvimodel
