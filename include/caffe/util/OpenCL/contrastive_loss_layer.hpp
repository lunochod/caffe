#ifndef __OPENCL_BNLL_LAYER_HPP__
#define __OPENCL_BNLL_LAYER_HPP__

#include <CL/cl.h>
#include <glog/logging.h>

#include <caffe/util/OpenCL/OpenCLDevice.hpp>
#include <caffe/util/OpenCL/OpenCLManager.hpp>
#include <caffe/util/OpenCL/OpenCLPlatform.hpp>
#include <caffe/util/OpenCL/OpenCLSupport.hpp>

#include <iostream>   // NOLINT(*)
#include <sstream>    // NOLINT(*)
#include <string>

namespace caffe {

namespace OpenCL {

template<typename T> bool clCLLForward(
    const int count,
    const int channels,
    const T margin,
    const T alpha,
    const T* y,
    const T* diff,
    const T* dist_sq,
    T *bottom_diff);
}  // namespace OpenCL

}  // namespace caffe

#endif  // __OPENCL_BNLL_LAYER_HPP__
