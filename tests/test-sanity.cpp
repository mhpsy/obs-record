#include "test-framework.h"

TEST(sanity_math) { CHECK(1 + 1 == 2); CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9); }
