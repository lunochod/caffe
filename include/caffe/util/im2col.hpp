#ifndef _CAFFE_UTIL_IM2COL_HPP_
#define _CAFFE_UTIL_IM2COL_HPP_

namespace caffe {

template <typename Dtype>
void im2col_cpu(const Dtype* data_im, const int channels,
    const int height, const int width, const int kernel_h, const int kernel_w,
    const int pad_h, const int pad_w, const int stride_h,
    const int stride_w, Dtype* data_col);

template <typename Dtype>
void col2im_cpu(const Dtype* data_col, const int channels,
    const int height, const int width, const int patch_h, const int patch_w,
    const int pad_h, const int pad_w, const int stride_h,
    const int stride_w, Dtype* data_im);

template <typename Dtype>
void im2col_gpu(const Dtype* data_im, const int channels,
    const int height, const int width, const int kernel_h, const int kernel_w,
    const int pad_h, const int pad_w, const int stride_h,
    const int stride_w, Dtype* data_col);

template<typename Dtype>
void im2col_gpu(
    const Dtype* data_im, const size_t data_im_step,
    const int channels, const int height, const int width,
		const int kernel_h, const int kernel_w,
		const int pad_h, const int pad_w,
		const int stride_h, const int stride_w,
		Dtype* data_col, const size_t data_col_step);

template<typename Dtype>
void im2col_gpu(
    const Dtype* data_im, const int bottom_step,
    const int num_images, const int num_channels, const int height, const int width,
		const int kernel_h, const int kernel_w,
		const int pad_h, const int pad_w,
		const int stride_h, const int stride_w,
		Dtype* data_col, const int top_step);

template <typename Dtype>
void col2im_gpu(const Dtype* data_col, const int channels,
    const int height, const int width, const int patch_h, const int patch_w,
    const int pad_h, const int pad_w, const int stride_h,
    const int stride_w, Dtype* data_im);

template<typename Dtype>
void col2im_gpu(const Dtype* data_col, const int data_col_step, const int channels, const int height, const int width, const int patch_h, const int patch_w, const int pad_h, const int pad_w, const int stride_h, const int stride_w,
		Dtype* data_im, const int data_im_step);

template<typename Dtype>
void col2im_gpu(const Dtype* data_col, const int top_step, const int col_number, const int channels, const int height, const int width, const int patch_h, const int patch_w, const int pad_h, const int pad_w, const int stride_h,
		const int stride_w, Dtype* data_im, const int bottom_step);

}  // namespace caffe

#endif  // CAFFE_UTIL_IM2COL_HPP_
