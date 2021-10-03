#include <climits>

#include "tensor.hpp"
#include "common.hpp"
#include "syncedmem.hpp"
#include "math_functions.hpp"
#include "flatbuffers/flatbuffers.h"

namespace mynet {

template <typename Dtype>
void Tensor<Dtype>::Reshape(const int num, const int channels, const int height,
    const int width) {
  std::vector<int> shape(4);
  shape[0] = num;
  shape[1] = channels;
  shape[2] = height;
  shape[3] = width;
  Reshape(shape);
}

template <typename Dtype>
void Tensor<Dtype>::Reshape(const std::vector<int>& shape) {
  CHECK_LE(shape.size(), kMaxTensorAxes);
  count_ = 1;
  shape_.resize(shape.size());
  if (!shape_data_ || shape_data_->size() < shape.size() * sizeof(int)) {
    shape_data_.reset(new SyncedMemory(shape.size() * sizeof(int)));
  }
  int* shape_data = static_cast<int*>(shape_data_->mutable_cpu_data());
  for (size_t i = 0; i < shape.size(); ++i) {
    CHECK_GE(shape[i], 0);
    if (count_ != 0) {
      CHECK_LE(shape[i], INT_MAX / count_) << "Tensor size exceeds INT_MAX";
    }
    count_ *= shape[i];
    shape_[i] = shape[i];
    shape_data[i] = shape[i];
  }
  if (count_ > capacity_) {
    capacity_ = count_;
    data_.reset(new SyncedMemory(capacity_ * sizeof(Dtype)));
    diff_.reset(new SyncedMemory(capacity_ * sizeof(Dtype)));
  }
}

template <typename Dtype>
void Tensor<Dtype>::Reshape(const TensorShape* shape) {
  auto shape_dim = shape->dim();
  CHECK_LE(shape_dim->size(), kMaxTensorAxes);
  std::vector<int> shape_vec(shape_dim->size());
  for (size_t i = 0; i < shape_dim->size(); ++i) {
    shape_vec[i] = shape_dim->Get(i);
  }
  Reshape(shape_vec);
}

template <typename Dtype>
void Tensor<Dtype>::ReshapeLike(const Tensor<Dtype>& other) {
  Reshape(other.shape());
}

template <typename Dtype>
Tensor<Dtype>::Tensor(const int num, const int channels, const int height,
    const int width)
  // capacity_ must be initialized before calling Reshape
  : capacity_(0) {
  Reshape(num, channels, height, width);
}

template <typename Dtype>
Tensor<Dtype>::Tensor(const std::vector<int>& shape)
  // capacity_ must be initialized before calling Reshape
  : capacity_(0) {
  Reshape(shape);
}

template <typename Dtype>
const Dtype* Tensor<Dtype>::cpu_data() const {
  CHECK(data_);
  return (const Dtype*)data_->cpu_data();
}

template <typename Dtype>
void Tensor<Dtype>::set_cpu_data(Dtype* data) {
  CHECK(data);
  // Make sure CPU and GPU sizes remain equal
  size_t size = count_ * sizeof(Dtype);
  if (data_->size() != size) {
    data_.reset(new SyncedMemory(size));
    diff_.reset(new SyncedMemory(size));
  }
  data_->set_cpu_data(data);
}

template <typename Dtype>
const Dtype* Tensor<Dtype>::cpu_diff() const {
  CHECK(diff_);
  return (const Dtype*)diff_->cpu_data();
}

template <typename Dtype>
Dtype* Tensor<Dtype>::mutable_cpu_data() {
  CHECK(data_);
  return static_cast<Dtype*>(data_->mutable_cpu_data());
}

template <typename Dtype>
Dtype* Tensor<Dtype>::mutable_cpu_diff() {
  CHECK(diff_);
  return static_cast<Dtype*>(diff_->mutable_cpu_data());
}

template <typename Dtype>
void Tensor<Dtype>::ShareData(const Tensor& other) {
  CHECK_EQ(count_, other.count());
  data_ = other.data();
}

template <typename Dtype>
void Tensor<Dtype>::ShareDiff(const Tensor& other) {
  CHECK_EQ(count_, other.count());
  diff_ = other.diff();
}

// The "update" method is used for parameter tensors in a Net, which are stored
// as Tensor<float> or Tensor<double> -- hence we do not define it for
// Tensor<int> or Tensor<unsigned int>.
template <> void Tensor<unsigned int>::Update() { NOT_IMPLEMENTED; }
template <> void Tensor<int>::Update() { NOT_IMPLEMENTED; }

template <typename Dtype>
void Tensor<Dtype>::Update() {
  // We will perform update based on where the data is located.
  switch (data_->head()) {
  case SyncedMemory::HEAD_AT_CPU:
    // perform computation on CPU
    mynet_axpy<Dtype>(count_, Dtype(-1),
        static_cast<const Dtype*>(diff_->cpu_data()),
        static_cast<Dtype*>(data_->mutable_cpu_data()));
    break;
  case SyncedMemory::SYNCED:
    break;
  default:
    LOG(FATAL) << "Syncedmem not initialized.";
  }
}

template <> unsigned int Tensor<unsigned int>::asum_data() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <> int Tensor<int>::asum_data() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <typename Dtype>
Dtype Tensor<Dtype>::asum_data() const {
  if (!data_) { return 0; }
  switch (data_->head()) {
  case SyncedMemory::HEAD_AT_CPU:
    return mynet_cpu_asum(count_, cpu_data());
  case SyncedMemory::SYNCED:
  case SyncedMemory::UNINITIALIZED:
    return 0;
  default:
    LOG(FATAL) << "Unknown SyncedMemory head state: " << data_->head();
  }
  return 0;
}

template <> unsigned int Tensor<unsigned int>::asum_diff() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <> int Tensor<int>::asum_diff() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <typename Dtype>
Dtype Tensor<Dtype>::asum_diff() const {
  if (!diff_) { return 0; }
  switch (diff_->head()) {
  case SyncedMemory::HEAD_AT_CPU:
    return mynet_cpu_asum(count_, cpu_diff());
  case SyncedMemory::SYNCED:
  case SyncedMemory::UNINITIALIZED:
    return 0;
  default:
    LOG(FATAL) << "Unknown SyncedMemory head state: " << diff_->head();
  }
  return 0;
}

template <> unsigned int Tensor<unsigned int>::sumsq_data() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <> int Tensor<int>::sumsq_data() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <typename Dtype>
Dtype Tensor<Dtype>::sumsq_data() const {
  Dtype sumsq;
  const Dtype* data;
  if (!data_) { return 0; }
  switch (data_->head()) {
  case SyncedMemory::HEAD_AT_CPU:
    data = cpu_data();
    sumsq = mynet_cpu_dot(count_, data, data);
    break;
  case SyncedMemory::SYNCED:
    break;
  case SyncedMemory::UNINITIALIZED:
    return 0;
  default:
    LOG(FATAL) << "Unknown SyncedMemory head state: " << data_->head();
  }
  return sumsq;
}

template <> unsigned int Tensor<unsigned int>::sumsq_diff() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <> int Tensor<int>::sumsq_diff() const {
  NOT_IMPLEMENTED;
  return 0;
}

template <typename Dtype>
Dtype Tensor<Dtype>::sumsq_diff() const {
  Dtype sumsq;
  const Dtype* diff;
  if (!diff_) { return 0; }
  switch (diff_->head()) {
  case SyncedMemory::HEAD_AT_CPU:
    diff = cpu_diff();
    sumsq = mynet_cpu_dot(count_, diff, diff);
    break;
  case SyncedMemory::SYNCED:
  case SyncedMemory::UNINITIALIZED:
    return 0;
  default:
    LOG(FATAL) << "Unknown SyncedMemory head state: " << data_->head();
  }
  return sumsq;
}

template <> void Tensor<unsigned int>::scale_data(unsigned int scale_factor) {
  NOT_IMPLEMENTED;
}

template <> void Tensor<int>::scale_data(int scale_factor) {
  NOT_IMPLEMENTED;
}

template <typename Dtype>
void Tensor<Dtype>::scale_data(Dtype scale_factor) {
  Dtype* data;
  if (!data_) { return; }
  switch (data_->head()) {
  case SyncedMemory::HEAD_AT_CPU:
    data = mutable_cpu_data();
    mynet_scal(count_, scale_factor, data);
    return;
  case SyncedMemory::SYNCED:
  case SyncedMemory::UNINITIALIZED:
    return;
  default:
    LOG(FATAL) << "Unknown SyncedMemory head state: " << data_->head();
  }
}

template <> void Tensor<unsigned int>::scale_diff(unsigned int scale_factor) {
  NOT_IMPLEMENTED;
}

template <> void Tensor<int>::scale_diff(int scale_factor) {
  NOT_IMPLEMENTED;
}

template <typename Dtype>
void Tensor<Dtype>::scale_diff(Dtype scale_factor) {
  Dtype* diff;
  if (!diff_) { return; }
  switch (diff_->head()) {
  case SyncedMemory::HEAD_AT_CPU:
    diff = mutable_cpu_diff();
    mynet_scal(count_, scale_factor, diff);
    return;
  case SyncedMemory::SYNCED:
  case SyncedMemory::UNINITIALIZED:
    return;
  default:
    LOG(FATAL) << "Unknown SyncedMemory head state: " << diff_->head();
  }
}

template <typename Dtype>
bool Tensor<Dtype>::ShapeEquals(const TensorFlat* other) {
  if (other->num() || other->channels() ||
      other->height() || other->width()) {
    // Using deprecated 4D Tensor dimensions --
    // shape is (num, channels, height, width).
    // Note: we do not use the normal Tensor::num(), Tensor::channels(), etc.
    // methods as these index from the beginning of the Tensor shape, where legacy
    // parameter Tensors were indexed from the end of the Tensor shape (e.g., bias
    // Tensor shape (1 x 1 x 1 x N), IP layer weight Tensor shape (1 x 1 x M x N)).

    return shape_.size() <= 4 &&
           LegacyShape(-4) == other->num() &&
           LegacyShape(-3) == other->channels() &&
           LegacyShape(-2) == other->height() &&
           LegacyShape(-1) == other->width();
  }

  if (other->shape() == nullptr) return false;

  auto other_shape_dim = other->shape()->dim();
  if (other_shape_dim == nullptr) return false;

  std::vector<int> other_shape(other_shape_dim->size());
  for (size_t i = 0; i < other_shape_dim->size(); ++i) {
    other_shape[i] = other_shape_dim->Get(i);
  }
  return shape_ == other_shape;
}

template <typename Dtype>
void Tensor<Dtype>::CopyFrom(const Tensor& source, bool copy_diff, bool reshape) {
  if (source.count() != count_ || source.shape() != shape_) {
    if (reshape) {
      ReshapeLike(source);
    } else {
      LOG(FATAL) << "Trying to copy Tensors of different sizes.";
    }
  }
  switch (Mynet::mode()) {
  case Mynet::CPU:
    if (copy_diff) {
      mynet_copy(count_, source.cpu_diff(),
          static_cast<Dtype*>(diff_->mutable_cpu_data()));
    } else {
      mynet_copy(count_, source.cpu_data(),
          static_cast<Dtype*>(data_->mutable_cpu_data()));
    }
    break;
  default:
    LOG(FATAL) << "Unknown mynet mode.";
  }
}

template <typename Dtype>
void Tensor<Dtype>::FromFlat(const TensorFlat* proto, bool reshape) {
  if (reshape) {
    std::vector<int> shape;
    if (proto->num() || proto->channels() ||
        proto->height() || proto->width()) {
      // Using deprecated 4D Tensor dimensions --
      // shape is (num, channels, height, width).
      shape.resize(4);
      shape[0] = proto->num();
      shape[1] = proto->channels();
      shape[2] = proto->height();
      shape[3] = proto->width();
    } else {
      auto proto_shape_dim = proto->shape()->dim();
      shape.resize(proto_shape_dim->size());
      for (size_t i = 0; i < proto_shape_dim->size(); ++i) {
        shape[i] = proto_shape_dim->Get(i);
      }
    }
    Reshape(shape);
  } else {
    CHECK(ShapeEquals(proto)) << "shape mismatch (reshape not set)";
  }
  // copy data
  Dtype* data_vec = mutable_cpu_data();
  auto proto_double_data = proto->double_data();
  auto proto_data = proto->data();
  if (proto_double_data->size() > 0) {
    CHECK_EQ(count_, proto_double_data->size());
    for (int i = 0; i < count_; ++i) {
      data_vec[i] = proto_double_data->Get(i);
    }
  } else {
    CHECK_EQ(count_, proto_data->size());
    for (int i = 0; i < count_; ++i) {
      data_vec[i] =proto_data->Get(i);
    }
  }

  auto proto_double_diff = proto->double_diff();
  auto proto_diff = proto->diff();
  if (proto_double_diff->size() > 0) {
    CHECK_EQ(count_, proto_double_diff->size());
    Dtype* diff_vec = mutable_cpu_diff();
    for (int i = 0; i < count_; ++i) {
      diff_vec[i] = proto_double_diff->Get(i);
    }
  } else if (proto_diff->size() > 0) {
    CHECK_EQ(count_, proto_diff->size());
    Dtype* diff_vec = mutable_cpu_diff();
    for (int i = 0; i < count_; ++i) {
      diff_vec[i] = proto_diff->Get(i);
    }
  }
}

template <>
std::vector<char> Tensor<double>::ToFlat(bool write_diff) const {
  flatbuffers::FlatBufferBuilder flatbuffer_builder;
  auto tensor_shape = CreateTensorShapeDirect(flatbuffer_builder, &shape_);

  const double* data_vec = cpu_data();
  std::vector<double> data(data_vec, data_vec + count_);
  const std::vector<double>* data_ptr = &data;
  const std::vector<double>* diff_ptr = nullptr;

  if (write_diff) {
    const double* diff_vec = cpu_diff();
    std::vector<double> diff(diff_vec, diff_vec + count_);
    diff_ptr = &diff;
  }
  auto tensor_flat = CreateTensorFlatDirect(flatbuffer_builder, 0, 0, 0, 0, nullptr, nullptr, tensor_shape, data_ptr, diff_ptr);
  flatbuffer_builder.Finish(tensor_flat);
  auto flatbuffer_builder_pointer = flatbuffer_builder.GetBufferPointer();
  return std::vector<char>(flatbuffer_builder_pointer, flatbuffer_builder_pointer + flatbuffer_builder.GetSize());
}

template <>
std::vector<char> Tensor<float>::ToFlat(bool write_diff) const {
  flatbuffers::FlatBufferBuilder flatbuffer_builder;
  auto tensor_shape = CreateTensorShapeDirect(flatbuffer_builder, &shape_);

  const float* data_vec = cpu_data();
  std::vector<float> data(data_vec, data_vec + count_);
  const std::vector<float>* data_ptr = &data;
  const std::vector<float>* diff_ptr = nullptr;

  if (write_diff) {
    const float* diff_vec = cpu_diff();
    std::vector<float> diff(diff_vec, diff_vec + count_);
    diff_ptr = &diff;
  }
  auto tensor_flat = CreateTensorFlatDirect(flatbuffer_builder, 0, 0, 0, 0, data_ptr, diff_ptr, tensor_shape, nullptr, nullptr);
  flatbuffer_builder.Finish(tensor_flat);
  auto flatbuffer_builder_pointer = flatbuffer_builder.GetBufferPointer();
  return std::vector<char>(flatbuffer_builder_pointer, flatbuffer_builder_pointer + flatbuffer_builder.GetSize());
}

INSTANTIATE_CLASS(Tensor);
template class Tensor<int>;
template class Tensor<unsigned int>;

}  // namespace mynet

