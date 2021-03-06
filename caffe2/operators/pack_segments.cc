/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "caffe2/operators/pack_segments.h"

namespace caffe2 {

template <>
template <typename T>
bool PackSegmentsOp<CPUContext>::DoRunWithType() {
  return DispatchHelper<
      TensorTypes2<char, int32_t, int64_t, float, std::string>,
      T>::call(this, Input(DATA));
}

template <>
template <typename T, typename Data_T>
bool PackSegmentsOp<CPUContext>::DoRunWithType2() {
  const auto& data = Input(DATA);
  const auto& lengths = Input(LENGTHS);
  auto* output = Output(0);
  Tensor<CPUContext>* presence_mask = nullptr;
  if (return_presence_mask_) {
    presence_mask = Output(1);
  }

  CAFFE_ENFORCE(data.ndim() >= 1, "DATA should be at least 1-D");
  CAFFE_ENFORCE(lengths.ndim() == 1, "LENGTH should be 1-D");

  // Find the length of the longest sequence.
  const T* l = lengths.template data<T>();
  T max_length = 0;
  for (T i = 0; i < lengths.dim(0); ++i) {
    max_length = std::max(max_length, l[i]);
  }

  auto shape = data.dims(); // Shape of output is batch_size x max_len x ...
  shape[0] = max_length;
  shape.insert(shape.begin(), lengths.size());
  output->Resize(shape);

  // create output tensor
  auto* out = static_cast<char*>(output->raw_mutable_data(data.meta()));

  bool* presence_mask_data = nullptr;
  if (return_presence_mask_) {
    // Shape of presence is batch_size x max_len
    std::vector<caffe2::TIndex> presence_shape{lengths.size(), max_length};
    presence_mask->Resize(presence_shape);
    presence_mask_data = presence_mask->template mutable_data<bool>();
  }

  if (!data.dim(0)) {
    // Return empty output (with the proper shape)
    return true;
  }

  // Do padding
  if (output->template IsType<float>()) {
    math::Set<float, CPUContext>(
        output->size(),
        padding_,
        output->template mutable_data<float>(),
        &context_);
  }
  if (return_presence_mask_) {
    memset(presence_mask_data, (int)false, presence_mask->size());
  }

  int block_size = data.size() / data.dim(0);
  int block_bytesize = data.nbytes() / data.dim(0);
  const auto* d = static_cast<const char*>(data.raw_data());
  int start = 0;
  for (int i = 0; i < lengths.dim(0); ++i) {
    context_.template CopyItems<CPUContext, CPUContext>(
        data.meta(),
        l[i] * block_size,
        d + block_bytesize * start,
        out + block_bytesize * max_length * i);
    if (return_presence_mask_) {
      memset(presence_mask_data + max_length * i, (int)true, l[i]);
    }
    start += l[i];
  }

  return true;
}

REGISTER_CPU_OPERATOR(PackSegments, PackSegmentsOp<CPUContext>);
REGISTER_CPU_OPERATOR(UnpackSegments, UnpackSegmentsOp<CPUContext>);

OPERATOR_SCHEMA(PackSegments)
    .NumInputs(2)
    .NumOutputs(1, 2)
    .SetDoc(
        "Map N dim tensor to N+1 dim based on length blob. Sequences that \
    are shorter than the longest sequence are padded with zeros.")
    .Input(
        0,
        "lengths",
        "1-d int/long tensor contains the length in each of the output.")
    .Input(1, "tensor", "N dim Tensor.")
    .Output(
        0,
        "packed_tensor",
        "N + 1 dim Tensor"
        "where dim(1) is the max length"
        ", dim(0) is the batch size.")
    .Output(
        1,
        "presence_mask",
        "2 dim boolean tensor"
        ", false where packed_tensor is padded, true otherwise.")
    .Arg(
        "pad_minf",
        "Padding number in the packed segments. Use true to pad \
    -infinity, otherwise pad zeros")
    .Arg(
        "return_presence_mask",
        "bool whether to return presence mask, false by default");
OPERATOR_SCHEMA(UnpackSegments)
    .NumInputs(2)
    .NumOutputs(1)
    .SetDoc("Map N+1 dim tensor to N dim based on length blob")
    .Input(
        0,
        "lengths",
        "1-d int/long tensor contains the length in each of the input.")
    .Input(1, "tensor", "N+1 dim Tensor.")
    .Output(0, "packed_tensor", "N dim Tensor");

class GetPackSegmentsGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    return SingleGradientDef(
        "UnpackSegments",
        "",
        vector<string>{I(0), GO(0)},
        vector<string>{GI(1)});
  }
};
REGISTER_GRADIENT(PackSegments, GetPackSegmentsGradient);

class GetUnpackSegmentsGradient : public GradientMakerBase {
  using GradientMakerBase::GradientMakerBase;
  vector<OperatorDef> GetGradientDefs() override {
    return SingleGradientDef(
        "PackSegments", "", vector<string>{I(0), GO(0)}, vector<string>{GI(1)});
  }
};
REGISTER_GRADIENT(UnpackSegments, GetUnpackSegmentsGradient);
} // namespace caffe2
