#include <stdio.h>
#include <queue>

#ifndef ZERO_JOBS_MALLOC
#define ZERO_JOBS_MALLOC(x) malloc(x)
#endif

#ifndef ZERO_JOBS_FREE
#define ZERO_JOBS_FREE(x) free(x)
#endif

#ifndef ZERO_JOBS_REALLOC
#define ZERO_JOBS_REALLOC(x, y) realloc(x, y)
#endif

#include "zero_fiber.h"

#define ZERO_ATOMIC_IMPL
#include "zero_atomic.h"

// -- template --
// #ifndef ZERO_JOBS_
// #define ZERO_JOBS_
// #endif

#ifndef ZERO_JOBS_SMALL_COUNT
#define ZERO_JOBS_SMALL_COUNT (128)
#endif

#ifndef ZERO_JOBS_LARGE_COUNT
#define ZERO_JOBS_LARGE_COUNT (32)
#endif

#ifndef ZERO_JOBS_SMALL_SIZE
#define ZERO_JOBS_SMALL_SIZE (64*1024)
#endif

#ifndef ZERO_JOBS_LARGE_SIZE
#define ZERO_JOBS_LARGE_SIZE (512*1024)
#endif

#ifndef ZERO_JOBS_TIMING_ERROR
#define ZERO_JOBS_TIMING_ERROR (0.000001)
#endif

void *basic_job(void *data);


struct job_t {
    struct zero_fiber_t* fiber;
    ZERO_ATOMIC(int) *status_counter;
};

struct job_waiting_t {
    job_t job;

    enum {
        JOB_WAIT_TIMER,
        JOB_WAIT_COUNTER_ZERO,
        JOB_WAIT_DATA_ZERO
    } condition;

    union {
        double end_time;
        void* data_address;
    };
};

thread_local std::queue<job_t> jobs;
thread_local std::queue<job_t> yielded_jobs;
thread_local std::queue<job_waiting_t> waiting_jobs;
thread_local struct job_t *job_current = nullptr;
thread_local double latest_time = 0.0;

job_t *zero_jobs_small_pool = NULL;
job_t *zero_jobs_large_pool = NULL;

ZERO_ATOMIC(job_t*) *zero_jobs_small_free_table = NULL;
ZERO_ATOMIC(job_t*) *zero_jobs_large_free_table = NULL;

// [jobs_run] should take a floating point number for the current
// time it should pull the jobs in [jobs] as well as any available
// to run in [waiting_jobs], remove them from their queues and place
// them into a temporary queue. The jobs in this temporary queue are
// then run as fibers, where they can be placed back into the main
// [jobs] queue or the [waiting_jobs] queue if their execution is
// dependent on a fulfilled condition.
//
// Bonus: jobs/waiting_jobs should be made into thread-safe queues
// whereby any thread can pull from them and any thread can place
// new jobs into them. Jobs should be exchangeable between threads.
// 
// This could potentially be done by using multiple spsc queues for
// every thread and using those for communication between the job
// scheduler and individual thread execution units. An idea to try.
// This has the potential for job execution order to get out of
// sync, so we can add timestamps to them at enqueue and sort by
// timestamp before execution to prevent that.
//
// 2/26/21: my current thought for the queuing mechanism is this:
// main thread allocates a large array(1024? 2048?) of a job_alloc_t.
// job_desc_t contains two members:
//  - atomic_int owning_thread;
//  - job_t job
// when any thread wants to allocate a new job, they must loop
// through the array to claim indices by atomic CAS on owning_thread
// only when owning_thread == 0
// 
// when a thread wishes to free a job, they write zero to the
// owning_thread field.
//
// the main thread also allocates a large array (4096?) of job_t* that
// exists to contain any available jobs. To push a job, the main
// thread first tries to grab a job_alloc_t from the allocation table.
// Once an allocation is available, the main thread loops through
// the available jobs array searching for the next index with a
// nullptr. An atomic CAS should be performed to write a modified
// form of the pointer to the found job_alloc_t.
//
// a secondary thread in idle execution state is just looping through
// the available jobs array, trying to snatch up the first non-nullptr
// found, at which point the job on the other end belongs to it.
//
// a similar array exists, owned by the main thread, for placing jobs
// that are either new or waiting. The same approach is taken with a
// secondary thread preparing any data into an allocation block and
// swapping the pointer to that allocation block onto the array at the
// first nullptr found.
//
// the main thread loops through the 'pushed_jobs' array regularly to
// pull jobs into a 'pending_jobs' queue, or if a job is ready to be
// run it can be immediately placed onto the 'available_jobs' queue.
//
// when any job has completed, the owning thread needs to find a slot
// in the job_alloc_t table to place it back, ensuring data isn't
// leaking and that further jobs can be allocated.
//
// These job arrays have the following operations:
//
// - T* try_take([optional] max_iterations)
//   tries to take the first available object using CAS
//   returns nullptr on failure
//
// - bool try_give(T* , [optional] max_iterations)
//   tries to give an object back into the first available nullptr slot
//   returns false on failure
//
// TODO(Wynter): We aren't currently cleaning up job memory. We
// should check to see if a job has finished executing and
// potentially free it.
//
// TODO(Wynter): In association with job memory free'ing, we can
// also pool job memory allocations, which would allow us to reuse
// the chunks instead of having to constantly malloc/free, which
// could become very costly very quickly.
void jobs_run(double time) {
    latest_time = time;
    std::queue<job_t> running_jobs;

    bool run_queueing = true;

    // TODO(Wynter): The below TODO is no longer true as I've
    // implemented another workaround utilizing a queue of
    // jobs that have yielded during that run. This does limit
    // each job to running only once per run. This may not be
    // an ideal solution in the long run however for the time
    // being we can just continue to call run as many times as
    // we want per frame to counter this issue.
    //
    // TODO(Wynter): If we don't check for time elapsed at
    // the end of this loop, jobs could queue themselves
    // into an endless loop. We could take in a callback
    // that returns true/false for if the job queueing
    // system should exit and run that check at the end
    // of a job burst
    while(run_queueing) {
        int total_jobs = 0;

        int num_jobs = jobs.size();
        int num_waiting_jobs = waiting_jobs.size();
        total_jobs += num_jobs + num_waiting_jobs;

//        printf("Running %i/%i jobs after %f\n", num_jobs, num_waiting_jobs, time);

        while( jobs.size() ) {
            running_jobs.push( jobs.front() );
            jobs.pop();
        }

        int waiting_size = waiting_jobs.size();
        for(int i = 0; i < waiting_size; i++) {
            auto wait_job = std::move(waiting_jobs.front());
            waiting_jobs.pop();

            bool still_waiting = false;

            switch(wait_job.condition) {
                case job_waiting_t::JOB_WAIT_TIMER:
                    if(time >= wait_job.end_time - ZERO_JOBS_TIMING_ERROR) {
//                        printf("running job at %f awaiting %f\n", time, wait_job.end_time);
                        running_jobs.push(wait_job.job);
                    }
                    else {
//                        printf("waiting job at %f until %f\n", time, wait_job.end_time);
                        still_waiting = true;
                    }
                break;
                case job_waiting_t::JOB_WAIT_COUNTER_ZERO:
                    if(wait_job.data_address && ZERO_ATOMIC_LOAD((ZERO_ATOMIC(int)*)wait_job.data_address) == 0) {
                        running_jobs.push(wait_job.job);
                    }
                    else still_waiting = true;
                break;
                case job_waiting_t::JOB_WAIT_DATA_ZERO:
                    // TODO(Wynter): Implement wait on zero at address
                break;
            }
            if(still_waiting) {
                waiting_jobs.push(wait_job);
            }
        }

        if(!running_jobs.size()) {
            run_queueing = false;
        }
        else {
//            printf("jobs: t[%i], r[%ld], w[%ld]\n",
//                    total_jobs,
//                    running_jobs.size(),
//                    waiting_jobs.size());
            while( running_jobs.size() ) {
                job_t job = running_jobs.front();
                running_jobs.pop();

                job_current = &job;
                zero_fiber_resume(job.fiber, NULL);
                
                if(!zero_fiber_is_active(job.fiber) && job.status_counter) {
                    ZERO_ATOMIC_DECREMENT(job.status_counter);
                }
            }
        }
    }
    while( yielded_jobs.size() ) {
        jobs.push(yielded_jobs.front());
        yielded_jobs.pop();
    }
}

ZERO_ATOMIC(int) *job_counter_make() {
    return new ZERO_ATOMIC(int)(0);
}

//
int job_pool_init() {
    job_t *zero_jobs_small_pool = (job_t*) ZERO_JOBS_MALLOC(ZERO_JOBS_SMALL_COUNT * sizeof(job_t));
    job_t *zero_jobs_large_pool = (job_t*) ZERO_JOBS_MALLOC(ZERO_JOBS_LARGE_COUNT * sizeof(job_t));

    zero_jobs_small_free_table = (ZERO_ATOMIC(job_t*)*) ZERO_JOBS_MALLOC(ZERO_JOBS_SMALL_COUNT * sizeof(job_t*));
    zero_jobs_large_free_table = (ZERO_ATOMIC(job_t*)*) ZERO_JOBS_MALLOC(ZERO_JOBS_LARGE_COUNT * sizeof(job_t*));

    for(size_t slot = 0; slot < ZERO_JOBS_SMALL_COUNT; slot++) {
        zero_jobs_small_free_table[slot] = &zero_jobs_small_pool[slot];
        job_t * j = (job_t*)ZERO_ATOMIC_LOAD( zero_jobs_small_free_table + slot );
        j->fiber = zero_fiber_make("", ZERO_JOBS_SMALL_SIZE, NULL, NULL);
    }

    for(size_t slot = 0; slot < ZERO_JOBS_LARGE_COUNT; slot++) {
        zero_jobs_large_free_table[slot] = &zero_jobs_large_pool[slot];
        job_t * j = (job_t*)ZERO_ATOMIC_LOAD( zero_jobs_large_free_table + slot );
        j->fiber = zero_fiber_make("", ZERO_JOBS_LARGE_SIZE, NULL, NULL);
    }
    
    return 0;
}

//
job_t* job_alloc(zero_entrypoint_t entrypoint, zero_userdata_t data) {
    job_t* job = NULL;

    for(size_t slot = 0; slot < ZERO_JOBS_SMALL_COUNT; slot++) {
        if( (job = (job_t*) ZERO_ATOMIC_LOAD(&zero_jobs_small_free_table[slot])) != NULL) {
            if(ZERO_ATOMIC_CAS(zero_jobs_small_free_table + slot, job, (job_t*) NULL)) {
                zero_context_derive(job->fiber->context, job->fiber->stack_size, entrypoint);
                job->fiber->entrypoint = entrypoint;
                job->fiber->userdata = data;
                job->fiber->status = ZERO_FIBER_STARTED;
                // memset(job->fiber->context, 0, job->fiber->stack_size);
                return job;
            }
        }
    }

    return NULL;
}

//
job_t* job_alloc_large(zero_entrypoint_t entrypoint, zero_userdata_t data) {
    job_t* job = NULL;

    for(size_t slot = 0; slot < ZERO_JOBS_SMALL_COUNT; slot++) {
        if( (job = (job_t*) ZERO_ATOMIC_LOAD(&zero_jobs_large_free_table[slot])) != NULL) {
            if(ZERO_ATOMIC_CAS(&zero_jobs_large_free_table[slot], job, (job_t*) NULL)) {
                job->fiber->entrypoint = entrypoint;
                job->fiber->userdata = data;
                // memset(job->fiber->context, 0, job->fiber->stack_size);
                return job;
            }
        }
    }

    return NULL;
}

//
void job_free(job_t* job) {
    size_t size = job->fiber->stack_size;
    job->fiber->entrypoint = NULL;

    ZERO_ATOMIC(job_t*)* table = NULL;
    
    if(size == ZERO_JOBS_SMALL_SIZE) {
        table = zero_jobs_small_free_table;
    }
    else if(size == ZERO_JOBS_LARGE_SIZE) {
        table = zero_jobs_large_free_table;
    }
    else {
        // TODO(Wynter): It should never get here
        //   If it does, the API user is likely doing something very wrong
        //   please error out here
    }

    // loop through job pool searching for first nullptr slot
    // we can use CAS here because if a slot isn't nullptr, nothing is written
    for(size_t slot = 0; slot < ZERO_JOBS_SMALL_COUNT; slot++) {
        if( (job = (job_t*) ZERO_ATOMIC_LOAD(&table[slot])) == NULL) {
            if(ZERO_ATOMIC_CAS(&table[slot], NULL, (job_t*) job)) {
                return;
            }
        }
    }
}

// void job_create(zero_entrypoint_t job_entrypoint, std::atomic<int> *counter) {
void job_create(zero_entrypoint_t job_entrypoint, ZERO_ATOMIC(int) *counter) {
    job_t job = {0};
    job.fiber = zero_fiber_make("", 4*1024, job_entrypoint, NULL);
    
    if(counter) {
        job.status_counter = counter;
        ZERO_ATOMIC_INCREMENT(job.status_counter);
    } else {
        job.status_counter = nullptr;
    }
    
    jobs.push(job);
}
void job_yield() {
    yielded_jobs.push(*job_current);
    zero_fiber_yield(nullptr);
}

void job_wait(double time) {
    job_waiting_t wait = { 0 };
    wait.job = *job_current;
    wait.condition = job_waiting_t::JOB_WAIT_TIMER;
    wait.end_time = latest_time + time;
    waiting_jobs.push(wait);
    zero_fiber_yield(nullptr);
}

void job_wait_on_condition(ZERO_ATOMIC(int) *counter) {
    job_waiting_t wait = { 0 };
    wait.job = *job_current;
    wait.condition = job_waiting_t::JOB_WAIT_COUNTER_ZERO;
    wait.data_address = (void*)counter;
    waiting_jobs.push(wait);
    zero_fiber_yield(nullptr);
}

void job_wait_zero() {

}
