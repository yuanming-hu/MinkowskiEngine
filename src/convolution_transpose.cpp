/*  Copyright (c) Chris Choy (chrischoy@ai.stanford.edu).
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 *  Please cite "4D Spatio-Temporal ConvNets: Minkowski Convolutional Neural
 *  Networks", CVPR'19 (https://arxiv.org/abs/1904.08755) if you use any part
 *  of the code.
 */
#include "common.hpp"

#include "convolution.cuh"
#include "convolution.hpp"

#include <pybind11/pybind11.h>

template <uint8_t D, typename Dtype, typename Itype>
void ConvolutionTransposeForwardCPU(
    at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager) {
  CoordsManager<D, Itype> *p_coords_manager =
      py_coords_manager.cast<CoordsManager<D, Itype> *>();
  auto in_out = p_coords_manager->setupAndReturnInOutPerKernel(
      tensor_strides, strides, kernel_sizes, dilations, region_type, offsets,
      py_in_coords_key, py_out_coords_key, true);

  if (in_feat.size(1) != kernel.size(1)) {
    throw std::invalid_argument(
        Formatter() << "Input feature size and kernel size mismatch");
  }

  int out_nrows = p_coords_manager->getCoordsSize(py_out_coords_key);
  out_feat.resize_({out_nrows, kernel.size(2)});
  out_feat.zero_();

  ConvolutionForwardKernelCPU<Dtype, Itype>(
      in_feat.data<Dtype>(), in_feat.size(1), out_feat.data<Dtype>(),
      out_feat.size(1), kernel.data<Dtype>(), std::get<0>(in_out),
      std::get<1>(in_out));
}

template <uint8_t D, typename Dtype, typename Itype>
void ConvolutionTransposeBackwardCPU(
    at::Tensor in_feat, at::Tensor grad_in_feat, at::Tensor grad_out_feat,
    at::Tensor kernel, at::Tensor grad_kernel, std::vector<int> tensor_strides,
    std::vector<int> strides, std::vector<int> kernel_sizes,
    std::vector<int> dilations, int region_type, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager) {
  CoordsManager<D, Itype> *p_coords_manager =
      py_coords_manager.cast<CoordsManager<D, Itype> *>();
  bool reverse_map = false;
  InOutMapKey rev_map_key = p_coords_manager->getMapHashKey(
      tensor_strides, strides, kernel_sizes, dilations, region_type,
      py_out_coords_key, py_in_coords_key, false);
  InOutMapKey map_key = p_coords_manager->getMapHashKey(
      tensor_strides, strides, kernel_sizes, dilations, region_type,
      py_in_coords_key, py_out_coords_key, true);

  // Check if the reverse map exists first
  if (p_coords_manager->in_maps.find(rev_map_key) !=
      p_coords_manager->in_maps.end())
    reverse_map = true;

  grad_in_feat.resize_as_(in_feat);
  grad_in_feat.zero_();
  grad_kernel.resize_as_(kernel);
  grad_kernel.zero_();

  if (!reverse_map)
    ConvolutionBackwardKernelCPU<Dtype, Itype>(
        in_feat.data<Dtype>(), grad_in_feat.data<Dtype>(), in_feat.size(1),
        grad_out_feat.data<Dtype>(), grad_out_feat.size(1),
        kernel.data<Dtype>(), grad_kernel.data<Dtype>(),
        p_coords_manager->in_maps[map_key],
        p_coords_manager->out_maps[map_key]);
  else
    ConvolutionBackwardKernelCPU<Dtype, Itype>(
        in_feat.data<Dtype>(), grad_in_feat.data<Dtype>(), in_feat.size(1),
        grad_out_feat.data<Dtype>(), grad_out_feat.size(1),
        kernel.data<Dtype>(), grad_kernel.data<Dtype>(),
        p_coords_manager->out_maps[rev_map_key],
        p_coords_manager->in_maps[rev_map_key]);
}

#ifndef CPU_ONLY
template <uint8_t D, typename Dtype, typename Itype>
void ConvolutionTransposeForwardGPU(
    at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager) {
  CoordsManager<D, Itype> *p_coords_manager =
      py_coords_manager.cast<CoordsManager<D, Itype> *>();
  auto in_out = p_coords_manager->setupAndReturnInOutPerKernel(
      tensor_strides, strides, kernel_sizes, dilations, region_type, offsets,
      py_in_coords_key, py_out_coords_key, true);

  if (in_feat.size(1) != kernel.size(1)) {
    throw std::invalid_argument(
        Formatter() << "Input feature size and kernel size mismatch");
  }

  int out_nrows = p_coords_manager->getCoordsSize(py_out_coords_key);
  out_feat.resize_({out_nrows, kernel.size(2)});
  out_feat.zero_();

  Itype * d_scr = p_coords_manager->getScratchGPUMemory(
      2 * (p_coords_manager->getMaxMapSize(in_out)));

  cublasHandle_t handle =
      THCState_getCurrentBlasHandle(at::globalContext().getTHCState());

  ConvolutionForwardKernelGPU<Dtype, Itype>(
      in_feat.data<Dtype>(), in_feat.size(1), out_feat.data<Dtype>(),
      out_feat.size(1), kernel.data<Dtype>(), std::get<0>(in_out),
      std::get<1>(in_out), out_nrows, d_scr, handle,
      at::cuda::getCurrentCUDAStream());
}

template <uint8_t D, typename Dtype, typename Itype>
void ConvolutionTransposeBackwardGPU(
    at::Tensor in_feat, at::Tensor grad_in_feat, at::Tensor grad_out_feat,
    at::Tensor kernel, at::Tensor grad_kernel, std::vector<int> tensor_strides,
    std::vector<int> strides, std::vector<int> kernel_sizes,
    std::vector<int> dilations, int region_type, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager) {
  CoordsManager<D, Itype> *p_coords_manager =
      py_coords_manager.cast<CoordsManager<D, Itype> *>();
  bool reverse_map = false;
  InOutMapKey rev_map_key = p_coords_manager->getMapHashKey(
      tensor_strides, strides, kernel_sizes, dilations, region_type,
      py_out_coords_key, py_in_coords_key, false);
  InOutMapKey map_key = p_coords_manager->getMapHashKey(
      tensor_strides, strides, kernel_sizes, dilations, region_type,
      py_in_coords_key, py_out_coords_key, true);

  // Check if the reverse map exists first
  if (p_coords_manager->in_maps.find(rev_map_key) !=
      p_coords_manager->in_maps.end())
    reverse_map = true;

  grad_in_feat.resize_as_(in_feat);
  grad_in_feat.zero_();
  grad_kernel.resize_as_(kernel);
  grad_kernel.zero_();

  Itype * d_scr = p_coords_manager->getScratchGPUMemory(
      2 * (p_coords_manager->getMaxMapSize(p_coords_manager->in_maps[map_key])));

  cublasHandle_t handle =
      THCState_getCurrentBlasHandle(at::globalContext().getTHCState());

  if (!reverse_map)
    ConvolutionBackwardKernelGPU<Dtype, Itype>(
        in_feat.data<Dtype>(), grad_in_feat.data<Dtype>(), in_feat.size(1),
        grad_out_feat.data<Dtype>(), grad_out_feat.size(1),
        kernel.data<Dtype>(), grad_kernel.data<Dtype>(),
        p_coords_manager->in_maps[map_key], p_coords_manager->out_maps[map_key],
        grad_out_feat.size(0), d_scr, handle, at::cuda::getCurrentCUDAStream());
  else
    ConvolutionBackwardKernelGPU<Dtype, Itype>(
        in_feat.data<Dtype>(), grad_in_feat.data<Dtype>(), in_feat.size(1),
        grad_out_feat.data<Dtype>(), grad_out_feat.size(1),
        kernel.data<Dtype>(), grad_kernel.data<Dtype>(),
        p_coords_manager->out_maps[rev_map_key],
        p_coords_manager->in_maps[rev_map_key], grad_out_feat.size(0), d_scr,
        handle, at::cuda::getCurrentCUDAStream());
}
#endif

template <typename Dtype, typename Itype>
void DimSwitchConvolutionTransposeForwardCPU(
    int D, at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager) {
  SWITCH_DIM_TYPES(ConvolutionTransposeForwardCPU, Dtype, Itype, in_feat,
                   out_feat, kernel, tensor_strides, strides, kernel_sizes,
                   dilations, region_type, offsets, py_in_coords_key,
                   py_out_coords_key, py_coords_manager);
}

template void DimSwitchConvolutionTransposeForwardCPU<float, int32_t>(
    int D, at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager);

template void DimSwitchConvolutionTransposeForwardCPU<double, int32_t>(
    int D, at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager);

template <typename Dtype, typename Itype>
void DimSwitchConvolutionTransposeBackwardCPU(
    int D, at::Tensor in_feat, at::Tensor grad_in_feat,
    at::Tensor grad_out_feat, at::Tensor kernel, at::Tensor grad_kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    py::object py_in_coords_key, py::object py_out_coords_key,
    py::object py_coords_manager) {
  SWITCH_DIM_TYPES(ConvolutionTransposeBackwardCPU, Dtype, Itype, in_feat,
                   grad_in_feat, grad_out_feat, kernel, grad_kernel,
                   tensor_strides, strides, kernel_sizes, dilations,
                   region_type, py_in_coords_key, py_out_coords_key,
                   py_coords_manager);
}

template void DimSwitchConvolutionTransposeBackwardCPU<float, int32_t>(
    int D, at::Tensor in_feat, at::Tensor grad_in_feat,
    at::Tensor grad_out_feat, at::Tensor kernel, at::Tensor grad_kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    py::object py_in_coords_key, py::object py_out_coords_key,
    py::object py_coords_manager);

template void DimSwitchConvolutionTransposeBackwardCPU<double, int32_t>(
    int D, at::Tensor in_feat, at::Tensor grad_in_feat,
    at::Tensor grad_out_feat, at::Tensor kernel, at::Tensor grad_kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    py::object py_in_coords_key, py::object py_out_coords_key,
    py::object py_coords_manager);

#ifndef CPU_ONLY
template <typename Dtype, typename Itype>
void DimSwitchConvolutionTransposeForwardGPU(
    int D, at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager) {
  SWITCH_DIM_TYPES(ConvolutionTransposeForwardGPU, Dtype, Itype, in_feat,
                   out_feat, kernel, tensor_strides, strides, kernel_sizes,
                   dilations, region_type, offsets, py_in_coords_key,
                   py_out_coords_key, py_coords_manager);
}

template void DimSwitchConvolutionTransposeForwardGPU<float, int32_t>(
    int D, at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager);

template void DimSwitchConvolutionTransposeForwardGPU<double, int32_t>(
    int D, at::Tensor in_feat, at::Tensor out_feat, at::Tensor kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    at::Tensor offsets, py::object py_in_coords_key,
    py::object py_out_coords_key, py::object py_coords_manager);

template <typename Dtype, typename Itype>
void DimSwitchConvolutionTransposeBackwardGPU(
    int D, at::Tensor in_feat, at::Tensor grad_in_feat,
    at::Tensor grad_out_feat, at::Tensor kernel, at::Tensor grad_kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    py::object py_in_coords_key, py::object py_out_coords_key,
    py::object py_coords_manager) {
  SWITCH_DIM_TYPES(ConvolutionTransposeBackwardGPU, Dtype, Itype, in_feat,
                   grad_in_feat, grad_out_feat, kernel, grad_kernel,
                   tensor_strides, strides, kernel_sizes, dilations,
                   region_type, py_in_coords_key, py_out_coords_key,
                   py_coords_manager);
}

template void DimSwitchConvolutionTransposeBackwardGPU<float, int32_t>(
    int D, at::Tensor in_feat, at::Tensor grad_in_feat,
    at::Tensor grad_out_feat, at::Tensor kernel, at::Tensor grad_kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    py::object py_in_coords_key, py::object py_out_coords_key,
    py::object py_coords_manager);

template void DimSwitchConvolutionTransposeBackwardGPU<double, int32_t>(
    int D, at::Tensor in_feat, at::Tensor grad_in_feat,
    at::Tensor grad_out_feat, at::Tensor kernel, at::Tensor grad_kernel,
    std::vector<int> tensor_strides, std::vector<int> strides,
    std::vector<int> kernel_sizes, std::vector<int> dilations, int region_type,
    py::object py_in_coords_key, py::object py_out_coords_key,
    py::object py_coords_manager);
#endif
