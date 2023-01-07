#include <doctest/doctest.h>
#define ZERO_FIBER_DEBUG 1
#include <zero/zero_fiber.h>
#include <iostream>

TEST_CASE("Fibers") {
    SUBCASE("Run basic fiber") {
        auto fiber_basic = [](zero_userdata_t data) -> zero_userdata_t {
            std::cout << "Starting basic fiber " << (uint64_t) data << std::endl;
            zero_userdata_t data2 = zero_fiber_yield((zero_userdata_t) 1);
            std::cout << "Resuming basic fiber " << (uint64_t) data2 << std::endl;
            zero_userdata_t data3 = zero_fiber_yield((zero_userdata_t) 2);
            std::cout << "Resuming basic fiber " << (uint64_t) data3 << std::endl;
            zero_userdata_t data4 = zero_fiber_yield((zero_userdata_t) 3);
            std::cout << "Resuming basic fiber " << (uint64_t) data4 << std::endl;
            return (zero_userdata_t) 1;
        };
        zero_fiber_t* fiber = zero_fiber_make("fiber_basic", 64*1024, fiber_basic, (zero_userdata_t*) 1);

        REQUIRE(zero_fiber_resume(fiber, (zero_userdata_t) 1) == (zero_userdata_t) 1);
        REQUIRE(zero_fiber_resume(fiber, (zero_userdata_t) 2) == (zero_userdata_t) 2);
        REQUIRE(zero_fiber_resume(fiber, (zero_userdata_t) 3) == (zero_userdata_t) 3);
        REQUIRE(zero_fiber_resume(fiber, (zero_userdata_t) 4) == (zero_userdata_t) 1);
    }
}
