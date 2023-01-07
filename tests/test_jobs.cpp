#include <doctest/doctest.h>
//#define ZERO_FIBER_DEBUG 1
#include <zero/zero_jobs.h>
#include <iostream>

int counter = 0;
void *counter_job(void*) {
    while(true) {
        job_yield();
        counter++;
//        job_wait(1.0/500.0);
    }
    return nullptr;
}

void *basic_job(void*) {
    std::cout << "Running basic_job" << std::flush;
    job_wait(0.5);
    std::cout << "." << std::flush;
    job_wait(0.5);
    std::cout << "." << std::flush;
    job_wait(0.5);
    std::cout << "." << std::flush;
    job_wait(0.5);
    std::cout << "Waited two seconds" << std::endl;
    return nullptr;
}

TEST_CASE("Jobs") {
    job_pool_init();

    SUBCASE("Run basic job") {
        job_create(basic_job, nullptr);
        job_create(counter_job, nullptr);
        job_create([](zero_userdata_t data) -> zero_userdata_t {
                while(true) {
                    job_wait(1.0);
                    int counter_value = counter;
                    counter = 0;
                    REQUIRE(counter_value == 120);
                    printf("counter = %i\n", counter_value);
                }
                return nullptr;
            }, nullptr);

        double time = 0.0;
        double time_max = 3.0;
        double time_step = 1.0 / 120.0;

        while (time < time_max) {
//            std::cout << "[" << time << "] ";
            jobs_run(time);
            time += time_step;
//            std::cout << std::endl;
        }
    }

//    Rockit::MutableArray<std::string> testArray(10);
//
//    REQUIRE(testArray.Count() == 0);
//    REQUIRE(testArray.AllocatedSize() >= 10);
//
//    SUBCASE("Adding to the array increases Count") {
//        Rockit::Platform::LogInfo("MutableArray", "Test increasing Count");
//        testArray.Add("This is a test string");
//
//        REQUIRE(testArray.Count() == 1);
//        REQUIRE(testArray.AllocatedSize() >= 10);
//        REQUIRE(testArray[0] == "This is a test string");
//    }
//
//    SUBCASE("Removing from the array decreases Count") {
//        Rockit::Platform::LogInfo("MutableArray", "Test decreasing Count");
//        testArray.Remove(0);
//
//        REQUIRE(testArray.Count() == 0);
//        REQUIRE(testArray.AllocatedSize() >= 10);
//    }
//
//    SUBCASE("Adding many elements to the array increases AllocatedSize") {
//        Rockit::Platform::LogInfo("MutableArray", "Test increasing AllocatedSize");
//        REQUIRE(testArray.AllocatedSize() >= 10);
//        REQUIRE(testArray.AllocatedSize() < 14);
//
//        REQUIRE(testArray.Add("This") == 1);
//        REQUIRE(testArray.Add("is") == 2);
//        REQUIRE(testArray.Add("a") == 3);
//        REQUIRE(testArray.Add("test") == 4);
//        REQUIRE(testArray.Add("attempting") == 5);
//        REQUIRE(testArray.Add("to") == 6);
//        REQUIRE(testArray.Add("increase") == 7);
//        REQUIRE(testArray.Add("AllocatedSize") == 8);
//        REQUIRE(testArray.Add("of") == 9);
//        REQUIRE(testArray.Add("MutableArray") == 10);
//        REQUIRE(testArray.Add("by") == 11);
//        REQUIRE(testArray.Add("adding") == 12);
//        REQUIRE(testArray.Add("14") == 13);
//        REQUIRE(testArray.Add("elements") == 14);
//
//        REQUIRE(testArray.Count() == 14);
//        REQUIRE(testArray.AllocatedSize() >= 14);
//        REQUIRE(testArray[11] == "adding");
//        REQUIRE(testArray[13] == "elements");
//    }
}
