#include <map>
#include <string>

#include "ops_factory.hpp"
#include "mynet_test_main.hpp"
#include "flatbuffers/flatbuffers.h"

namespace mynet {

template <typename TypeParam>
class OpsFactoryTest : public MultiDeviceTest<TypeParam> {};

TYPED_TEST_CASE(OpsFactoryTest, TestDtypesAndDevices);

TYPED_TEST(OpsFactoryTest, TestCreateOps) {
  typedef typename TypeParam::Dtype Dtype;
  typename OpsRegistry<Dtype>::CreatorRegistry& registry =
      OpsRegistry<Dtype>::Registry();
  std::shared_ptr<Ops<Dtype>> ops;
  for (const auto& [k, v] : registry) {
    OpsParameterT ops_param;
    // Data layers expect a DB
    if (k == "Data") {
      continue;
    }
    
    ops_param.type = k;
    ops_param.conv_param = std::make_unique<ConvParameterT>();
    ops = OpsRegistry<Dtype>::CreateOps(&ops_param);
    EXPECT_EQ(k, ops->type());
  }
}

}  // namespace mynet