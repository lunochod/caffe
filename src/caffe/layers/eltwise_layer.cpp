#if defined(USE_OPENCL)
#include <caffe/util/OpenCL/definitions.hpp>
#include <caffe/util/OpenCL/eltwise_layer.hpp>
#include <caffe/util/OpenCL/OpenCLDevice.hpp>
#endif

#include <cfloat>
#include <string>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template<typename Dtype>
void EltwiseLayer<Dtype>::LayerSetUp(
    const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  CHECK(this->layer_param().eltwise_param().coeff_size() == 0
      || this->layer_param().eltwise_param().coeff_size() == bottom.size()) <<
  "Eltwise Layer takes one coefficient per bottom blob.";
  CHECK(!(this->layer_param().eltwise_param().operation()
          == EltwiseParameter_EltwiseOp_PROD
          && this->layer_param().eltwise_param().coeff_size())) <<
  "Eltwise layer only takes coefficients for summation.";
  op_ = this->layer_param_.eltwise_param().operation();
  // Blob-wise coefficients for the elementwise operation.
  coeffs_ = vector<Dtype>(bottom.size(), 1);
  if (this->layer_param().eltwise_param().coeff_size()) {
    for (int i = 0; i < bottom.size(); ++i) {
      coeffs_[i] = this->layer_param().eltwise_param().coeff(i);
    }
  }
  stable_prod_grad_ = this->layer_param_.eltwise_param().stable_prod_grad();
}

template<typename Dtype>
void EltwiseLayer<Dtype>::Reshape(
    const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  for (int i = 1; i < bottom.size(); ++i) {
    CHECK(bottom[i]->shape() == bottom[0]->shape());
  }
  top[0]->ReshapeLike(*bottom[0]);
  // If max operation, we will initialize the vector index part.
  if (this->layer_param_.eltwise_param().operation()
      == EltwiseParameter_EltwiseOp_MAX && top.size() == 1) {
    max_idx_.Reshape(bottom[0]->shape());
  }
}

template<typename Dtype>
void EltwiseLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  int* mask = NULL;
  const Dtype* bottom_data_a = NULL;
  const Dtype* bottom_data_b = NULL;
  const int count = top[0]->count();
  Dtype* top_data = top[0]->mutable_cpu_data();
  switch (op_) {
    case EltwiseParameter_EltwiseOp_PROD:
      caffe_mul(count, bottom[0]->cpu_data(), bottom[1]->cpu_data(), top_data);
      for (int i = 2; i < bottom.size(); ++i) {
        caffe_mul(count, top_data, bottom[i]->cpu_data(), top_data);
      }
      break;
    case EltwiseParameter_EltwiseOp_SUM:
      caffe_set(count, Dtype(0), top_data);
      // TODO(shelhamer) does BLAS optimize to sum for coeff = 1?
      for (int i = 0; i < bottom.size(); ++i) {
        caffe_axpy(count, coeffs_[i], bottom[i]->cpu_data(), top_data);
      }
      break;
    case EltwiseParameter_EltwiseOp_MAX:
      // Initialize
      mask = max_idx_.mutable_cpu_data();
      caffe_set(count, -1, mask);
      caffe_set(count, Dtype(-FLT_MAX), top_data);
      // bottom 0 & 1
      bottom_data_a = bottom[0]->cpu_data();
      bottom_data_b = bottom[1]->cpu_data();
      for (int idx = 0; idx < count; ++idx) {
        if (bottom_data_a[idx] > bottom_data_b[idx]) {
          top_data[idx] = bottom_data_a[idx];  // maxval
          mask[idx] = 0;  // maxid
        } else {
          top_data[idx] = bottom_data_b[idx];  // maxval
          mask[idx] = 1;  // maxid
        }
      }
      // bottom 2++
      for (int blob_idx = 2; blob_idx < bottom.size(); ++blob_idx) {
        bottom_data_b = bottom[blob_idx]->cpu_data();
        for (int idx = 0; idx < count; ++idx) {
          if (bottom_data_b[idx] > top_data[idx]) {
            top_data[idx] = bottom_data_b[idx];  // maxval
            mask[idx] = blob_idx;  // maxid
          }
        }
      }
      break;
    default:
      LOG(FATAL)<< "Unknown elementwise operation.";
    }
  }

template<typename Dtype>
void EltwiseLayer<Dtype>::Backward_cpu(
    const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  const int* mask = NULL;
  const int count = top[0]->count();
  const Dtype* top_data = top[0]->cpu_data();
  const Dtype* top_diff = top[0]->cpu_diff();
  for (int i = 0; i < bottom.size(); ++i) {
    if (propagate_down[i]) {
      const Dtype* bottom_data = bottom[i]->cpu_data();
      Dtype* bottom_diff = bottom[i]->mutable_cpu_diff();
      switch (op_) {
        case EltwiseParameter_EltwiseOp_PROD:
          if (stable_prod_grad_) {
            bool initialized = false;
            for (int j = 0; j < bottom.size(); ++j) {
              if (i == j) {
                continue;
              }
              if (!initialized) {
                caffe_copy(
                    count,
                    bottom[j]->cpu_data(),
                    bottom_diff);
                initialized = true;
              } else {
                caffe_mul(
                    count,
                    bottom[j]->cpu_data(),
                    bottom_diff,
                    bottom_diff);
              }
            }
          } else {
            caffe_div(count, top_data, bottom_data, bottom_diff);
          }
          caffe_mul(count, bottom_diff, top_diff, bottom_diff);
          break;
        case EltwiseParameter_EltwiseOp_SUM:
          if (coeffs_[i] == Dtype(1)) {
            caffe_copy(count, top_diff, bottom_diff);
          } else {
            caffe_cpu_scale(count, coeffs_[i], top_diff, bottom_diff);
          }
          break;
        case EltwiseParameter_EltwiseOp_MAX:
          mask = max_idx_.cpu_data();
          for (int index = 0; index < count; ++index) {
            Dtype gradient = 0;
            if (mask[index] == i) {
              gradient += top_diff[index];
            }
            bottom_diff[index] = gradient;
          }
          break;
        default:
          LOG(FATAL)<< "Unknown elementwise operation.";
        }
      }
    }
  }

#if defined(USE_OPENCL)

namespace OpenCL {

template<typename T>
bool clMaxForward(
    const int nthreads,
    const T* bottom_data_a,
    const T* bottom_data_b,
    const int blob_idx,
    T* top_data,
    int* mask) {
  OpenCLDevice& device = OpenCLManager::CurrentPlatform()->CurrentDevice();

  std::string kernel_name = clGetKernelName<T>("MaxForward");

  cl_command_queue* queue = device.getCurrentCommandQueue();
  if (!queue) {
    LOG(ERROR)<< device.name() << "> failed to get OpenCL command queue";
    return false;
  }

  cl_kernel* kernel = device.getKernel(kernel_name);
  if (kernel == NULL) {
    return false;
  }

  CL_SET_KERNEL_ARG
  CL_SET_TYPE_KERNEL_ARG(int, nthreads, kernel)
  CL_SET_ARRAY_KERNEL_ARG(&bottom_data_a, kernel)
  CL_SET_ARRAY_KERNEL_ARG(&bottom_data_b, kernel)
  CL_SET_TYPE_KERNEL_ARG(int, blob_idx, kernel)
  CL_SET_ARRAY_KERNEL_ARG(&top_data, kernel)
  CL_SET_ARRAY_KERNEL_ARG(&mask, kernel)

  size_t global = CAFFE_GET_GLOBAL_WORKITEMS(nthreads, OPENCL_LOCAL_SIZE);
  size_t local = CAFFE_GET_LOCAL_WORKITEMS(nthreads, OPENCL_LOCAL_SIZE);

  err = clEnqueueNDRangeKernel(*queue, *kernel, 1, NULL,
                               &global, &local, 0, NULL, NULL);
  if (err != CL_SUCCESS) {
    LOG(ERROR) << "Failed to enqueue kernel '"
               << kernel_name.c_str()
               << "' on GPU "
               << device.name()
               << " : " << caffe::OpenCL::what(err);
    return false;
  }
  DLOG(INFO) << "kernel '"
             << kernel_name.c_str()
             << "' executed on GPU "
             << device.name();

  CL_SET_KERNEL_ARG_END

  return true;
}
template bool clMaxForward<float>(
    const int nthreads,
    const float* bottom_data_a,
    const float* bottom_data_b,
    const int blob_idx,
    float* top_data,
    int* mask);
template bool clMaxForward<double>(
    const int nthreads,
    const double* bottom_data_a,
    const double* bottom_data_b,
    const int blob_idx,
    double* top_data,
    int* mask);

template<typename T>
bool clMaxBackward(
    const int nthreads,
    const T* top_diff,
    const int blob_idx,
    const int* mask,
    T* bottom_diff) {
  OpenCLDevice& device = OpenCLManager::CurrentPlatform()->CurrentDevice();

  std::string kernel_name = clGetKernelName<T>("MaxBackward");

  cl_command_queue* queue = device.getCurrentCommandQueue();
  if (!queue) {
    LOG(ERROR)<< device.name() << "> failed to get OpenCL command queue";
    return false;
  }

  cl_kernel* kernel = device.getKernel(kernel_name);
  if (kernel == NULL) {
    return false;
  }

  CL_SET_KERNEL_ARG
  CL_SET_TYPE_KERNEL_ARG(int, nthreads, kernel)
  CL_SET_ARRAY_KERNEL_ARG(&top_diff, kernel)
  CL_SET_TYPE_KERNEL_ARG(int, blob_idx, kernel)
  CL_SET_ARRAY_KERNEL_ARG(&mask, kernel)
  CL_SET_ARRAY_KERNEL_ARG(&bottom_diff, kernel)

  size_t global = CAFFE_GET_GLOBAL_WORKITEMS(nthreads, OPENCL_LOCAL_SIZE);
  size_t local = CAFFE_GET_LOCAL_WORKITEMS(nthreads, OPENCL_LOCAL_SIZE);

  err = clEnqueueNDRangeKernel(*queue, *kernel, 1, NULL,
                               &global, &local, 0, NULL, NULL);
  if (err != CL_SUCCESS) {
    LOG(ERROR) << "Failed to enqueue kernel '"
               << kernel_name.c_str()
               << "' on GPU "
               << device.name()
               << " : " << caffe::OpenCL::what(err);
    return false;
  }
  DLOG(INFO) << "kernel '"
             << kernel_name.c_str()
             << "' executed on GPU "
             << device.name();

  CL_SET_KERNEL_ARG_END

  return true;
}
template bool clMaxBackward<float>(
    const int nthreads,
    const float* top_diff,
    const int blob_idx,
    const int* mask,
    float* bottom_diff);
template bool clMaxBackward<double>(
    const int nthreads,
    const double* top_diff,
    const int blob_idx,
    const int* mask,
    double* bottom_diff);

}  // namespace OpenCL

template<typename Dtype>
void EltwiseLayer<Dtype>::Forward_gpu(
    const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  int* mask = NULL;
  const int count = (top)[0]->count();
  Dtype* top_data = (top)[0]->mutable_gpu_data();
  switch (op_) {
    case EltwiseParameter_EltwiseOp_PROD:
      caffe_gpu_mul(count, bottom[0]->gpu_data(),
                    bottom[1]->gpu_data(), top_data);
      for (int i = 2; i < bottom.size(); ++i) {
        caffe_gpu_mul(count, top_data, bottom[i]->gpu_data(), top_data);
      }
      break;
    case EltwiseParameter_EltwiseOp_SUM:
      caffe_gpu_set(count, Dtype(0.), top_data);
      // TODO(shelhamer) does cuBLAS optimize to sum for coeff = 1?
      for (int i = 0; i < bottom.size(); ++i) {
        caffe_gpu_axpy(count, coeffs_[i], bottom[i]->gpu_data(), top_data);
      }
      break;
    case EltwiseParameter_EltwiseOp_MAX:
      mask = max_idx_.mutable_gpu_data();
      /*
       // NOLINT_NEXT_LINE(whitespace/operators)
       MaxForward<Dtype>  << <CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>(
       count, bottom[0]->gpu_data(), bottom[1]->gpu_data(), 0, top_data, mask);
       for (int i = 2; i < bottom.size(); ++i) {
       // NOLINT_NEXT_LINE(whitespace/operators)
       MaxForward<Dtype> << <CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>(
       count, top_data, bottom[i]->gpu_data(), i-1, top_data, mask);
       }
       */

      BOOL_CHECK(
          caffe::OpenCL::clMaxForward<Dtype>(
              count,
              bottom[0]->gpu_data(),
              bottom[1]->gpu_data(),
              0,
              top_data,
              mask));
      for (int i = 2; i < bottom.size(); ++i) {
        BOOL_CHECK(
            caffe::OpenCL::clMaxForward<Dtype>(
                count,
                top_data,
                bottom[i]->gpu_data(),
                i - 1,
                top_data,
                mask));
      }

      break;
    default:
      LOG(FATAL)<< "Unknown elementwise operation.";
    }
  }

template<typename Dtype>
void EltwiseLayer<Dtype>::Backward_gpu(
    const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  const int* mask = NULL;
  const int count = top[0]->count();
  const Dtype* top_data = top[0]->gpu_data();
  const Dtype* top_diff = top[0]->gpu_diff();
  for (int i = 0; i < bottom.size(); ++i) {
    if (propagate_down[i]) {
      const Dtype* bottom_data = (bottom)[i]->gpu_data();
      Dtype* bottom_diff = (bottom)[i]->mutable_gpu_diff();
      switch (op_) {
        case EltwiseParameter_EltwiseOp_PROD:
          if (stable_prod_grad_) {
            bool initialized = false;
            for (int j = 0; j < bottom.size(); ++j) {
              if (i == j) {
                continue;
              }
              if (!initialized) {
                caffe_copy(count, (bottom)[j]->gpu_data(), bottom_diff);
                initialized = true;
              } else {
                caffe_gpu_mul(count, (bottom)[j]->gpu_data(),
                              bottom_diff, bottom_diff);
              }
            }
          } else {
            caffe_gpu_div(count, top_data, bottom_data, bottom_diff);
          }
          caffe_gpu_mul(count, bottom_diff, top_diff, bottom_diff);
          break;
        case EltwiseParameter_EltwiseOp_SUM:
          if (coeffs_[i] == Dtype(1.)) {
            caffe_copy(count, top_diff, bottom_diff);
          } else {
            caffe_gpu_scale(count, coeffs_[i], top_diff, bottom_diff);
          }
          break;
        case EltwiseParameter_EltwiseOp_MAX:
          mask = max_idx_.gpu_data();
          /*
           MaxBackward<Dtype>  // NOLINT_NEXT_LINE(whitespace/operators)
           << <CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>(
           count, top_diff, i, mask, bottom_diff);
           */
          BOOL_CHECK(
              caffe::OpenCL::clMaxBackward<Dtype>(count, top_diff, i,
                                                  mask, bottom_diff));
          break;
        default:
          LOG(FATAL)<< "Unknown elementwise operation.";
        }
      }
    }
  }

#endif

#if defined(CPU_ONLY) && !defined(USE_OPENCL)
  STUB_GPU(EltwiseLayer);
#endif

INSTANTIATE_CLASS(EltwiseLayer);
REGISTER_LAYER_CLASS(Eltwise);

}  // namespace caffe
