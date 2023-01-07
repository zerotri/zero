#ifndef ZERO_ATOMIC_INCLUDED
#define ZERO_ATOMIC_INCLUDED

#if defined(_WIN32)
#define ZERO_ATOMIC_WINDOWS (1)
#elif defined(__APPLE__)
#define ZERO_ATOMIC_APPLE (1)
#else
#define ZERO_ATOMIC_LINUX (1)
#endif

#if ZERO_ATOMIC_WINDOWS
#include <intrin.h>
#define ZERO_ATOMIC(x) volatile x
#define ZERO_ATOMIC_LOAD(x) InterlockedCompareExchange64((__int64*)x, (__int64)0, (__int64)0)
#define ZERO_ATOMIC_CAS(dest, expected, desired) _InterlockedCompareExchange64((volatile __int64*)dest, (__int64)desired, (__int64)expected)
#define ZERO_ATOMIC_SWAP(dest, value) _InterlockedExchange64((volatile long long*)dest, value)
#define ZERO_ATOMIC_INCREMENT(x) _InterlockedIncrement64((__int64*)x)
#define ZERO_ATOMIC_DECREMENT(x) _InterlockedDecrement64((__int64*)x)

#elif ZERO_ATOMIC_APPLE || ZERO_ATOMIC_LINUX
#ifdef __cplusplus
#include <atomic>
#define ZERO_ATOMIC(x) volatile x
// #define ZERO_ATOMIC_LOAD(x) __sync_val_compare_and_swap(x, 0, 0)
#define ZERO_ATOMIC_LOAD(x) (*(__typeof__(*x) *volatile) (x))
#define ZERO_ATOMIC_STORE(x, value) ((*(__typeof__(*x) *volatile) (x)) = (value))
#define ZERO_ATOMIC_CAS(dest, expected, desired) __sync_val_compare_and_swap(dest, expected, desired)
#define ZERO_ATOMIC_SWAP(dest, value) __sync_lock_test_and_set(dest, value)
#define ZERO_ATOMIC_INCREMENT(x) __sync_fetch_and_add(x, 1);
#define ZERO_ATOMIC_DECREMENT(x) __sync_fetch_and_sub(x, 1);
#else
#include <stdatomic.h>
#define ZERO_ATOMIC(x) _Atomic x
#define ZERO_ATOMIC_LOAD(x) atomic_load(x)
#define ZERO_ATOMIC_CAS(dest, exchange, desired) atomic_compare_exchange_weak(dest, expected, desired)
#define ZERO_ATOMIC_SWAP(dest, value) atomic_exchange(dest, value)
#define ZERO_ATOMIC_INCREMENT(x) atomic_fetch_add(x, 1);
#define ZERO_ATOMIC_DECREMENT(x) atomic_fetch_sub(x, 1);

#endif // __cplusplus
#endif // ZERO_ATOMIC_APPLE || ZERO_ATOMIC_LINUX

#endif // ZERO_ATOMIC_INCLUDED