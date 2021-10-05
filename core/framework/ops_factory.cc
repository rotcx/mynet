#include "ops.hpp"
#include "ops_factory.hpp"
#include "core/kernels/conv_ops.hpp"

namespace mynet {

// Get conv ops according to engine.
template <typename Dtype>
std::shared_ptr<Ops<Dtype>> GetConvOps(OpsParameterT* param) {
  auto conv_param = std::move(param->conv_param);
  DCHECK(conv_param) << "is nullptr !";
  auto engine = conv_param->engine;
  if (engine == Engine_DEFAULT) {
    engine = Engine_MYNET;
  }
  if (engine == Engine_MYNET) {
    return std::shared_ptr<Ops<Dtype>>(new ConvOps<Dtype>(param));
  } else {
    LOG(FATAL) << "ops " << param->name << " has unknown engine.";
    throw;  // Avoids missing return warning
  }
}

REGISTER_OPS_CREATOR(Conv, GetConvOps);

// Ops that use their constructor as their default creator should be
// registered in their corresponding cpp files. Do not register them here.
}  // namespace mynet
