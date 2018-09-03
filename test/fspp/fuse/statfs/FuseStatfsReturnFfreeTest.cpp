#include "testutils/FuseStatfsReturnTest.h"

using ::testing::WithParamInterface;
using ::testing::Values;

class FuseStatfsReturnFfreeTest: public FuseStatfsReturnTest<fsfilcnt64_t>, public WithParamInterface<fsfilcnt64_t> {
private:
  void set(struct ::statvfs *stat, fsfilcnt64_t value) override {
    stat->f_ffree = value;
  }
};
INSTANTIATE_TEST_CASE_P(FuseStatfsReturnFfreeTest, FuseStatfsReturnFfreeTest, Values(
    0,
    10,
    256,
    1024,
    4096
));

TEST_P(FuseStatfsReturnFfreeTest, ReturnedFfreeIsCorrect) {
  struct ::statvfs result = CallStatfsWithValue(GetParam());
  EXPECT_EQ(GetParam(), result.f_ffree);
}
