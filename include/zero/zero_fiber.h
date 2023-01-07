#ifndef ZERO_FIBER_INCLUDED

/*
    zero_fiber.h    -- simple cross-platform fibers

    Project URL: https://github.com/zerotri/zero

    Example:

        #define ZERO_FIBER_IMPL
        #define ZERO_FIBER_DEBUG 1
        #include "zero/zero_fiber.h"

        struct zero_fiber_t *fiber_a = NULL;
        struct zero_fiber_t *fiber_b = NULL;

        zero_userdata_t iterate(void) {
            uintptr_t step = 0;
            while(step < 10) {
                step = (uintptr_t) zero_fiber_yield((zero_userdata_t)++step);
                printf("yield %i\n", step);
            }
            
            return (zero_userdata_t)200;
        }

        int main(int argc, char** argv) {
            fiber_b = zero_fiber_make("", 4 * 1024, iterate);
            fiber_b->description = "B";
            
            fiber_a = zero_fiber_active();
            fiber_a->description = "A";

            uintptr_t step = 0;
            while(zero_fiber_is_active(fiber_b)) {
                step = (uintptr_t) zero_fiber_resume(fiber_b, (zero_userdata_t)++step);
                printf("resume: %i\n", step);
            }
        }


    Do this:
        #define ZERO_FIBER_IMPL
    before you include this file in *one* C or C++ file to create the
    implementation.

    Optionally provide the following defines with your own implementations:
    ZERO_FIBER_ASSERT(c)     - your own assert macro (default: assert(c))

    struct zero_fiber_t *zero_fiber_make(const char* name, size_t stack_size, zero_entrypoint_t entrypoint);
    
    void zero_fiber_delete(struct zero_fiber_t *fiber);
    
    struct zero_fiber_t *zero_fiber_active(void);
        Get the current fiber. If this is called outside of an active
        coroutine, it will derive one from the currently running thread.

    zero_userdata_t zero_fiber_resume(struct zero_fiber_t *coroutine, zero_userdata_t userdata);
    
    zero_userdata_t zero_fiber_yield(zero_userdata_t userdata);
    
    int zero_fiber_is_active(struct zero_fiber_t *fiber);

    ucoroutine_t uco_active(void);
        Get the current coroutine. If this is called outside of an active
        coroutine, it will derive one from the currently running thread.

    ucoroutine_t uco_derive(void* memory, unsigned int size, void (*entrypoint)(void));
        Create a coroutine from the provided info.

    ucoroutine_t uco_create(unsigned int size, void (*entrypoint)(void));
    void uco_delete(ucoroutine_t coroutine);
    void uco_switch(ucoroutine_t coroutine);
    int uco_serializable(void);

    Copyright (c) 2020 Wynter Woods

    Some parts of this code pulled from libco, written by Byuu.
*/

#define ZERO_FIBER_INCLUDED (1)
#include <stdint.h>

#ifndef ZERO_FIBER_API_DECL
#if defined(_WIN32) && defined(ZERO_FIBER_DLL) && defined(ZERO_FIBER_IMPL)
#define ZERO_FIBER_API_DECL __declspec(dllexport)
#elif defined(_WIN32) && defined(ZERO_FIBER_DLL)
#define ZERO_FIBER_API_DECL __declspec(dllimport)
#else
#define ZERO_FIBER_API_DECL extern
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__i386__) || defined(_M_X86)
#error("Currently not implemented for x86")
#define ZERO_FIBER_X86 (1)
#elif (defined(__amd64__) || defined(_M_AMD64) || defined(_M_X64))
#define ZERO_FIBER_X86_64 (1)
#elif defined(__arm__)
#error("Currently not implemented for arm32")
#define ZERO_FIBER_ARM32 (1)
#elif defined(__aarch64__)
//#error("Currently not implemented for arm64")
#define ZERO_FIBER_ARM64 (1)
#elif defined(__EMSCRIPTEN__)
#error("Currently not implemented for emscripten")
#define ZERO_FIBER_EMSCRIPTEN (1)
#endif

#if defined(_WIN32)
#define ZERO_FIBER_WINDOWS (1)
#elif defined(__APPLE__)
#define ZERO_FIBER_APPLE (1)
#else
#define ZERO_FIBER_LINUX (1)
#endif

enum zero_coroutine_status {
    ZERO_FIBER_STARTED,
    ZERO_FIBER_SUSPENDED,
    ZERO_FIBER_RUNNING,
    ZERO_FIBER_ENDED,
    ZERO_FIBER_ERROR
};

typedef void *zero_coroutine_t;
typedef void *zero_context_t;
typedef void *zero_userdata_t;
typedef void *(*zero_entrypoint_t)(void*);
typedef void (*zero_entrypoint_wrapper_t)();

struct zero_fiber_t {
    struct zero_fiber_t *caller;
    const char* description;
    zero_context_t context;
    zero_userdata_t userdata;
    zero_entrypoint_t entrypoint;
    enum zero_coroutine_status status;
    size_t stack_size;
};

ZERO_FIBER_API_DECL struct zero_fiber_t *zero_fiber_make(const char* name, size_t stack_size, zero_entrypoint_t entrypoint, zero_userdata_t data);
ZERO_FIBER_API_DECL void zero_fiber_delete(struct zero_fiber_t *fiber);
ZERO_FIBER_API_DECL struct zero_fiber_t *zero_fiber_active(void);
ZERO_FIBER_API_DECL zero_userdata_t zero_fiber_resume(struct zero_fiber_t *coroutine, zero_userdata_t userdata);
ZERO_FIBER_API_DECL zero_userdata_t zero_fiber_yield(zero_userdata_t userdata);
ZERO_FIBER_API_DECL int zero_fiber_is_active(struct zero_fiber_t *fiber);
ZERO_FIBER_API_DECL zero_userdata_t zero_fiber_active_data();
ZERO_FIBER_API_DECL zero_context_t zero_context_derive(void* memory, unsigned int size, zero_entrypoint_t entrypoint);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif // ZERO_FIBER_INCLUDED


/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef ZERO_FIBER_IMPL
#define ZERO_FIBER_IMPL_INCLUDED (1)

#ifndef ZERO_FIBER_API_IMPL
    #define ZERO_FIBER_API_IMPL
#endif

#if !defined(ZERO_FIBER_MALLOC) || !defined(ZERO_FIBER_FREE)
    #include <stdlib.h>
#endif
#if !defined(ZERO_FIBER_ASSERT)
    #define ZERO_FIBER_ASSERT(str) assert(str)
#endif
#if !defined(ZERO_FIBER_MALLOC)
    #define ZERO_FIBER_MALLOC(size) malloc(size)
#endif
#if !defined(ZERO_FIBER_FREE)
    #define ZERO_FIBER_FREE(ptr) free(ptr)
#endif

#ifndef _ZERO_FIBER_PRIVATE
    #if defined(__GNUC__) || defined(__clang__)
        #define _ZERO_FIBER_PRIVATE __attribute__((unused)) static
    #else
        #define _ZERO_FIBER_PRIVATE static
    #endif
#endif

#if defined(_MSC_VER)
    #define thread_local __declspec(thread)
#elif (__STDC_VERSION__ < 201112L || __cplusplus < 201103L) && (defined(__clang__) || defined(__GNUC__))
    #define ZERO_FIBER_THREAD_LOCAL __thread
#else
    #if defined(__STDC_VERSION__)
        #define ZERO_FIBER_THREAD_LOCAL
    #elif defined(__cplusplus)
        #include <threads.h>
        #define ZERO_FIBER_THREAD_LOCAL thread_local
    #endif
#endif

#if defined(_MSC_VER)
    //__declspec(allocate("." #name))
    #define ZERO_FIBER_SECTION(name) __pragma(code_seg("." #name))
    #define ZERO_FIBER_THREAD_LOCAL __declspec(thread)
#elif defined(__APPLE__)
    #define ZERO_FIBER_SECTION(name) __attribute__((section("__TEXT,__" #name)))
    #define ZERO_FIBER_THREAD_LOCAL __thread
#else
    #define ZERO_FIBER_SECTION(name) __attribute__((section("." #name "#")))
#endif

_ZERO_FIBER_PRIVATE void zero_fiber_return(zero_userdata_t);
_ZERO_FIBER_PRIVATE void zero_fiber_wrap_entrypoint();

/*== x86_64 =====================================================================*/
#if defined(ZERO_FIBER_X86_64)

static ZERO_FIBER_THREAD_LOCAL struct zero_fiber_t zero_fiber_main = {0};
static ZERO_FIBER_THREAD_LOCAL struct zero_fiber_t *zero_fiber_current = NULL;

static ZERO_FIBER_THREAD_LOCAL long long zero_context_active_buffer[64];
static ZERO_FIBER_THREAD_LOCAL zero_context_t zero_active_context = 0;
static void *(*_zero_co_swap)(zero_context_t, zero_context_t) = 0;

#if defined(ZERO_FIBER_WINDOWS)

ZERO_FIBER_SECTION(text);
static const unsigned char _zero_co_swap_function[4096] = {
    0x48, 0x89, 0x22,              /* mov [rdx],rsp          */
    0x48, 0x8b, 0x21,              /* mov rsp,[rcx]          */
    0x58,                          /* pop rax                */
    0x48, 0x89, 0x6a, 0x08,        /* mov [rdx+ 8],rbp       */
    0x48, 0x89, 0x72, 0x10,        /* mov [rdx+16],rsi       */
    0x48, 0x89, 0x7a, 0x18,        /* mov [rdx+24],rdi       */
    0x48, 0x89, 0x5a, 0x20,        /* mov [rdx+32],rbx       */
    0x4c, 0x89, 0x62, 0x28,        /* mov [rdx+40],r12       */
    0x4c, 0x89, 0x6a, 0x30,        /* mov [rdx+48],r13       */
    0x4c, 0x89, 0x72, 0x38,        /* mov [rdx+56],r14       */
    0x4c, 0x89, 0x7a, 0x40,        /* mov [rdx+64],r15       */
    0x0f, 0x29, 0x72, 0x50,        /* movaps [rdx+ 80],xmm6  */
    0x0f, 0x29, 0x7a, 0x60,        /* movaps [rdx+ 96],xmm7  */
    0x44, 0x0f, 0x29, 0x42, 0x70,  /* movaps [rdx+112],xmm8  */
    0x48, 0x83, 0xc2, 0x70,        /* add rdx,112            */
    0x44, 0x0f, 0x29, 0x4a, 0x10,  /* movaps [rdx+ 16],xmm9  */
    0x44, 0x0f, 0x29, 0x52, 0x20,  /* movaps [rdx+ 32],xmm10 */
    0x44, 0x0f, 0x29, 0x5a, 0x30,  /* movaps [rdx+ 48],xmm11 */
    0x44, 0x0f, 0x29, 0x62, 0x40,  /* movaps [rdx+ 64],xmm12 */
    0x44, 0x0f, 0x29, 0x6a, 0x50,  /* movaps [rdx+ 80],xmm13 */
    0x44, 0x0f, 0x29, 0x72, 0x60,  /* movaps [rdx+ 96],xmm14 */
    0x44, 0x0f, 0x29, 0x7a, 0x70,  /* movaps [rdx+112],xmm15 */
    0x48, 0x8b, 0x69, 0x08,        /* mov rbp,[rcx+ 8]       */
    0x48, 0x8b, 0x71, 0x10,        /* mov rsi,[rcx+16]       */
    0x48, 0x8b, 0x79, 0x18,        /* mov rdi,[rcx+24]       */
    0x48, 0x8b, 0x59, 0x20,        /* mov rbx,[rcx+32]       */
    0x4c, 0x8b, 0x61, 0x28,        /* mov r12,[rcx+40]       */
    0x4c, 0x8b, 0x69, 0x30,        /* mov r13,[rcx+48]       */
    0x4c, 0x8b, 0x71, 0x38,        /* mov r14,[rcx+56]       */
    0x4c, 0x8b, 0x79, 0x40,        /* mov r15,[rcx+64]       */
    0x0f, 0x28, 0x71, 0x50,        /* movaps xmm6, [rcx+ 80] */
    0x0f, 0x28, 0x79, 0x60,        /* movaps xmm7, [rcx+ 96] */
    0x44, 0x0f, 0x28, 0x41, 0x70,  /* movaps xmm8, [rcx+112] */
    0x48, 0x83, 0xc1, 0x70,        /* add rcx,112            */
    0x44, 0x0f, 0x28, 0x49, 0x10,  /* movaps xmm9, [rcx+ 16] */
    0x44, 0x0f, 0x28, 0x51, 0x20,  /* movaps xmm10,[rcx+ 32] */
    0x44, 0x0f, 0x28, 0x59, 0x30,  /* movaps xmm11,[rcx+ 48] */
    0x44, 0x0f, 0x28, 0x61, 0x40,  /* movaps xmm12,[rcx+ 64] */
    0x44, 0x0f, 0x28, 0x69, 0x50,  /* movaps xmm13,[rcx+ 80] */
    0x44, 0x0f, 0x28, 0x71, 0x60,  /* movaps xmm14,[rcx+ 96] */
    0x44, 0x0f, 0x28, 0x79, 0x70,  /* movaps xmm15,[rcx+112] */
    0xff, 0xe0,                    /* jmp rax                */
};

#include <windows.h>

#else

/* ABI: SystemV */
static const unsigned char _zero_co_swap_function[4096] = {
    0x48, 0x89, 0x26,        /* mov [rsi],rsp    */
    0x48, 0x8b, 0x27,        /* mov rsp,[rdi]    */
    0x58,                    /* pop rax          */
    0x48, 0x89, 0x6e, 0x08,  /* mov [rsi+ 8],rbp */
    0x48, 0x89, 0x5e, 0x10,  /* mov [rsi+16],rbx */
    0x4c, 0x89, 0x66, 0x18,  /* mov [rsi+24],r12 */
    0x4c, 0x89, 0x6e, 0x20,  /* mov [rsi+32],r13 */
    0x4c, 0x89, 0x76, 0x28,  /* mov [rsi+40],r14 */
    0x4c, 0x89, 0x7e, 0x30,  /* mov [rsi+48],r15 */
    0x48, 0x8b, 0x6f, 0x08,  /* mov rbp,[rdi+ 8] */
    0x48, 0x8b, 0x5f, 0x10,  /* mov rbx,[rdi+16] */
    0x4c, 0x8b, 0x67, 0x18,  /* mov r12,[rdi+24] */
    0x4c, 0x8b, 0x6f, 0x20,  /* mov r13,[rdi+32] */
    0x4c, 0x8b, 0x77, 0x28,  /* mov r14,[rdi+40] */
    0x4c, 0x8b, 0x7f, 0x30,  /* mov r15,[rdi+48] */
    0xff, 0xe0,              /* jmp rax          */
};

#include <unistd.h>
#include <sys/mman.h>

#endif

_ZERO_FIBER_PRIVATE void _zero_co_x86_64_init() {
    #if defined(ZERO_FIBER_WINDOWS)
        DWORD old_privileges;
        VirtualProtect((void*)_zero_co_swap_function, sizeof _zero_co_swap_function, PAGE_EXECUTE_READ, &old_privileges);
    #elif defined(ZERO_FIBER_LINUX) || defined(ZERO_FIBER_APPLE)
        unsigned long long addr = (unsigned long long)_zero_co_swap_function;
        unsigned long long base = addr - (addr % sysconf(_SC_PAGESIZE));
        unsigned long long size = (addr - base) + sizeof _zero_co_swap_function;
        mprotect((void*)base, size, PROT_READ | PROT_EXEC);
    #endif
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_x86_64_active(void) {
    if(!zero_active_context) zero_active_context = &zero_context_active_buffer;
    return zero_active_context;
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_x86_64_derive(void* memory, unsigned int size, zero_entrypoint_t entrypoint) {
    zero_context_t context;
    if(!_zero_co_swap) {
        _zero_co_x86_64_init();
        _zero_co_swap = (void *(*)(zero_context_t, zero_context_t))(void*)_zero_co_swap_function;
    }
    if(!zero_active_context) zero_active_context = &zero_context_active_buffer;

    if((context = (zero_context_t)memory)) {
        unsigned int offset = (size & ~15) - 32;
        long long *p = (long long*)((char*)context + offset);  /* seek to top of stack */
        *--p = (long long)zero_fiber_wrap_entrypoint;                         /* start of function */
        *(long long*)context = (long long)p;                   /* stack pointer */
    }

    return context;
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_x86_64_create(unsigned int size, zero_entrypoint_t entrypoint) {
    void* memory = ZERO_FIBER_MALLOC(size);
    if(!memory) return (zero_context_t)0;
    return _zero_co_x86_64_derive(memory, size, entrypoint);
}

_ZERO_FIBER_PRIVATE void _zero_co_x86_64_delete(zero_context_t context) {
    ZERO_FIBER_FREE(context);
}

_ZERO_FIBER_PRIVATE zero_userdata_t _zero_co_x86_64_switch(zero_context_t context) {
    zero_context_t zero_previous_context = zero_active_context;
    zero_userdata_t userdata = _zero_co_swap(zero_active_context = context, zero_previous_context);

    return userdata;
}

#endif

/*== x86_64 =====================================================================*/
#if defined(ZERO_FIBER_ARM64)

static ZERO_FIBER_THREAD_LOCAL struct zero_fiber_t zero_fiber_main = {0};
static ZERO_FIBER_THREAD_LOCAL struct zero_fiber_t *zero_fiber_current = NULL;

static ZERO_FIBER_THREAD_LOCAL long long zero_context_active_buffer[64];
static ZERO_FIBER_THREAD_LOCAL zero_context_t zero_active_context = 0;
static void *(*_zero_co_swap)(zero_context_t, zero_context_t) = 0;

/* ASM */
extern "C" void *co_switch_arm64(zero_context_t, zero_context_t);

#if defined(ZERO_FIBER_WINDOWS)
#else

/* ABI: SystemV */
asm (
    ".text\n"
    ".globl co_switch_arm64\n"
    ".globl _co_switch_arm64\n"
    "co_switch_arm64:\n"
    "_co_switch_arm64:\n"
    "  stp x8,  x9,  [x1]\n"
    "  stp x10, x11, [x1, #16]\n"
    "  stp x12, x13, [x1, #32]\n"
    "  stp x14, x15, [x1, #48]\n"
    "  str x19, [x1, #72]\n"
    "  stp x20, x21, [x1, #80]\n"
    "  stp x22, x23, [x1, #96]\n"
    "  stp x24, x25, [x1, #112]\n"
    "  stp x26, x27, [x1, #128]\n"
    "  stp x28, x29, [x1, #144]\n"
    "  mov x16, sp\n"
    "  stp x16, x30, [x1, #160]\n"

    "  ldp x8,  x9,  [x0]\n"
    "  ldp x10, x11, [x0, #16]\n"
    "  ldp x12, x13, [x0, #32]\n"
    "  ldp x14, x15, [x0, #48]\n"
    "  ldr x19, [x0, #72]\n"
    "  ldp x20, x21, [x0, #80]\n"
    "  ldp x22, x23, [x0, #96]\n"
    "  ldp x24, x25, [x0, #112]\n"
    "  ldp x26, x27, [x0, #128]\n"
    "  ldp x28, x29, [x0, #144]\n"
    "  ldp x16, x17, [x0, #160]\n"
    "  mov sp, x16\n"
    "  br x17\n"
    ".previous\n"
);

#include <unistd.h>
#include <sys/mman.h>

#endif

_ZERO_FIBER_PRIVATE void _zero_co_arm64_init() {
    #if defined(ZERO_FIBER_WINDOWS)
        DWORD old_privileges;
        VirtualProtect((void*)_zero_co_swap_function, sizeof _zero_co_swap_function, PAGE_EXECUTE_READ, &old_privileges);
    #elif defined(ZERO_FIBER_LINUX) || defined(ZERO_FIBER_APPLE)

    #endif
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_arm64_active(void) {
    if(!zero_active_context) zero_active_context = &zero_context_active_buffer;
    return zero_active_context;
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_arm64_derive(void* memory, unsigned int size, zero_entrypoint_t entrypoint) {
    zero_context_t context;
    size = (size + 1023) & ~1023;
#if HAVE_POSIX_MEMALIGN >= 1
    if (posix_memalign(&context, 1024, size + 512) < 0)
        return 0;
#else
    context = memalign(1024, size + 512);
#endif

    if (!context)
        return context;

    uint64_t *ptr = (uint64_t*)context;
    /* Non-volatiles.  */
    ptr[0]  = 0; /* x8  */
    ptr[1]  = 0; /* x9  */
    ptr[2]  = 0; /* x10 */
    ptr[3]  = 0; /* x11 */
    ptr[4]  = 0; /* x12 */
    ptr[5]  = 0; /* x13 */
    ptr[6]  = 0; /* x14 */
    ptr[7]  = 0; /* x15 */
    ptr[8]  = 0; /* padding */
    ptr[9]  = 0; /* x19 */
    ptr[10] = 0; /* x20 */
    ptr[11] = 0; /* x21 */
    ptr[12] = 0; /* x22 */
    ptr[13] = 0; /* x23 */
    ptr[14] = 0; /* x24 */
    ptr[15] = 0; /* x25 */
    ptr[16] = 0; /* x26 */
    ptr[17] = 0; /* x27 */
    ptr[18] = 0; /* x28 */
    ptr[20] = (uintptr_t)ptr + size + 512 - 16; /* x30, stack pointer */
    ptr[19] = ptr[20]; /* x29, frame pointer */
    ptr[21] = (uintptr_t)zero_fiber_wrap_entrypoint; /* PC (link register x31 gets saved here). */
//    ptr[22] = (uintptr_t)zero_fiber_return; /* PC (link register x31 gets saved here). */

//    *out_size = size + 512;
    return context;
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_arm64_create(unsigned int size, zero_entrypoint_t entrypoint) {
    void* memory = ZERO_FIBER_MALLOC(size);
    if(!memory) return (zero_context_t)0;

    if(!zero_active_context) zero_active_context = &zero_context_active_buffer;

    return _zero_co_arm64_derive(memory, size, entrypoint);
}

_ZERO_FIBER_PRIVATE void _zero_co_arm64_delete(zero_context_t context) {
    ZERO_FIBER_FREE(context);
}

_ZERO_FIBER_PRIVATE zero_userdata_t _zero_co_arm64_switch(zero_context_t context) {
    zero_context_t zero_previous_context = zero_active_context;
    zero_userdata_t userdata = co_switch_arm64(zero_active_context = context, zero_previous_context);

    return userdata;
}

#endif


#if defined(ZERO_FIBER_EMSCRIPTEN)

static ZERO_FIBER_THREAD_LOCAL struct zero_fiber_t zero_fiber_main = {0};
static ZERO_FIBER_THREAD_LOCAL struct zero_fiber_t *zero_fiber_current = NULL;

static ZERO_FIBER_THREAD_LOCAL long long zero_context_active_buffer[64];
static ZERO_FIBER_THREAD_LOCAL zero_context_t zero_active_context = 0;
static void *(*_zero_co_swap)(zero_context_t, zero_context_t) = 0;

emscripten_fiber_t

_ZERO_FIBER_PRIVATE void _zero_co_emscripten_init() {
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_emscripten_active(void) {
    if(!zero_active_context) zero_active_context = &zero_context_active_buffer;
    return zero_active_context;
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_emscripten_derive(void* memory, unsigned int size, zero_entrypoint_t entrypoint) {
    zero_context_t context;
    if(!_zero_co_swap) {
        _zero_co_x86_64_init();
        _zero_co_swap = (void *(*)(zero_context_t, zero_context_t))_zero_co_swap_function;
    }
    if(!zero_active_context) zero_active_context = &zero_context_active_buffer;

    if(context = (zero_context_t)memory) {
        unsigned int offset = (size & ~15) - 32;
        long long *p = (long long*)((char*)context + offset);  /* seek to top of stack */
        *--p = (long long)zero_fiber_return;                    /* if entrypoint returns, we need to return to the last fiber */
        *--p = (long long)entrypoint;                         /* start of function */
        *(long long*)context = (long long)p;                   /* stack pointer */
    }

    return context;
}

_ZERO_FIBER_PRIVATE zero_context_t _zero_co_emscripten_create(unsigned int size, zero_entrypoint_t entrypoint) {
    void* memory = ZERO_FIBER_MALLOC(size);
    if(!memory) return (zero_context_t)0;
    return _zero_co_emscripten_derive(memory, size, entrypoint);
}

_ZERO_FIBER_PRIVATE void _zero_co_emscripten_delete(zero_context_t context) {
    ZERO_FIBER_FREE(context);
}

_ZERO_FIBER_PRIVATE zero_userdata_t _zero_co_emscripten_switch(zero_context_t context) {
    register zero_context_t zero_previous_context = zero_active_context;
    zero_userdata_t userdata = _zero_co_swap(zero_active_context = context, zero_previous_context);
    return userdata;
}

#endif

_ZERO_FIBER_PRIVATE zero_context_t zero_context_active(void) {
    #if defined(ZERO_FIBER_X86)
    return _zero_co_x86_active();
    #elif defined(ZERO_FIBER_X86_64)
    return _zero_co_x86_64_active();
    #elif defined(ZERO_FIBER_ARM32)
    return _zero_co_arm32_active();
    #elif defined(ZERO_FIBER_ARM64)
    return _zero_co_arm64_active();
    #elif defined(ZERO_FIBER_EMSCRIPTEN)
    return _zero_co_emscripten_active();
    #endif
}

_ZERO_FIBER_PRIVATE zero_context_t zero_context_create(unsigned int size, zero_entrypoint_t entrypoint) {
    #if defined(ZERO_FIBER_X86)
    return _zero_co_x86_create(size, entrypoint);
    #elif defined(ZERO_FIBER_X86_64)
    return _zero_co_x86_64_create(size, entrypoint);
    #elif defined(ZERO_FIBER_ARM32)
    return _zero_co_arm32_create(size, entrypoint);
    #elif defined(ZERO_FIBER_ARM64)
    return _zero_co_arm64_create(size, entrypoint);
    #elif defined(ZERO_FIBER_EMSCRIPTEN)
    return _zero_co_emscripten_create(size, entrypoint);
    #endif
}

_ZERO_FIBER_PRIVATE void zero_context_delete(zero_context_t coroutine) {
    #if defined(ZERO_FIBER_X86)
    _zero_co_x86_delete(coroutine);
    #elif defined(ZERO_FIBER_X86_64)
    _zero_co_x86_64_delete(coroutine);
    #elif defined(ZERO_FIBER_ARM32)
    _zero_co_arm32_delete(coroutine);
    #elif defined(ZERO_FIBER_ARM64)
    _zero_co_arm64_delete(coroutine);
    #elif defined(ZERO_FIBER_EMSCRIPTEN)
    _zero_co_emscripten_delete(coroutine);
    #endif
}

_ZERO_FIBER_PRIVATE void *zero_context_switch(zero_context_t coroutine) {
    #if defined(ZERO_FIBER_X86)
    return _zero_co_x86_switch(coroutine);
    #elif defined(ZERO_FIBER_X86_64)
    return _zero_co_x86_64_switch(coroutine);
    #elif defined(ZERO_FIBER_ARM32)
    return _zero_co_arm32_switch(coroutine);
    #elif defined(ZERO_FIBER_ARM64)
    return _zero_co_arm64_switch(coroutine);
    #elif defined(ZERO_FIBER_EMSCRIPTEN)
    return _zero_co_emscripten_switch(coroutine);
    #endif
}

_ZERO_FIBER_PRIVATE void zero_fiber_return(zero_userdata_t userdata) {

    struct zero_fiber_t *fiber = zero_fiber_active();
//    zero_userdata_t returndata = fiber->userdata;
    zero_userdata_t returndata = userdata;
    fiber->status = ZERO_FIBER_ENDED;

#if ZERO_FIBER_DEBUG
    printf("fiber ended: %s\n", fiber->description);
    printf("userdata: %p\n", userdata);
#endif

    while(fiber->status == ZERO_FIBER_ENDED) {
        fiber = fiber->caller;
	}

    zero_fiber_current = fiber;
    fiber->userdata = returndata;

    zero_context_switch(fiber->context);
}

ZERO_FIBER_API_DECL struct zero_fiber_t *zero_fiber_active(void) {
    if(!zero_fiber_current) {
        zero_fiber_main.status = ZERO_FIBER_RUNNING;
        zero_fiber_main.context = zero_context_active();
        zero_fiber_main.description = "main";
        zero_fiber_current = &zero_fiber_main;
    }

    return zero_fiber_current;
}


ZERO_FIBER_API_DECL zero_context_t zero_context_derive(void* memory, unsigned int size, zero_entrypoint_t entrypoint) {
#if defined(ZERO_FIBER_X86)
    return _zero_co_x86_derive(memory, size, entrypoint);
#elif defined(ZERO_FIBER_X86_64)
    return _zero_co_x86_64_derive(memory, size, entrypoint);
#elif defined(ZERO_FIBER_ARM32)
    return _zero_co_arm32_derive(memory, size, entrypoint);
#elif defined(ZERO_FIBER_ARM64)
    return _zero_co_arm64_derive(memory, size, entrypoint);
#elif defined(ZERO_FIBER_EMSCRIPTEN)
    return _zero_co_emscripten_derive(memory, size, entrypoint);
#endif
}

_ZERO_FIBER_PRIVATE void zero_fiber_wrap_entrypoint() {
    zero_fiber_t* fiber = zero_fiber_active();
    zero_userdata_t data = fiber->entrypoint(fiber->userdata);
    printf("wrapper exiting with data %p\n", data);
    zero_fiber_return(data);
}

ZERO_FIBER_API_DECL struct zero_fiber_t *zero_fiber_make(const char* name, size_t stack_size, zero_entrypoint_t entrypoint, zero_userdata_t data) {

    // TODO(Wynter): should this be stack_size + sizeof(zero_fiber_t)? look into this
    struct zero_fiber_t* fiber = (struct zero_fiber_t*) ZERO_FIBER_MALLOC(stack_size);
    if(!fiber) return (struct zero_fiber_t*)NULL;

    fiber->entrypoint = entrypoint;
    fiber->status = ZERO_FIBER_STARTED;
    fiber->stack_size = stack_size;
    fiber->userdata = data;
    fiber->description = name;
    fiber->context = zero_context_create(stack_size, entrypoint);

    return fiber;
}

ZERO_FIBER_API_DECL zero_userdata_t zero_fiber_active_data() {
    struct zero_fiber_t *current_fiber = zero_fiber_active();

    if(current_fiber->status == ZERO_FIBER_ENDED) {
        return NULL;
    }

    return current_fiber->userdata;
}

ZERO_FIBER_API_DECL zero_userdata_t zero_fiber_resume(struct zero_fiber_t *coroutine, zero_userdata_t userdata) {
    struct zero_fiber_t *current_fiber = zero_fiber_active();

    if(coroutine->status == ZERO_FIBER_ENDED) {
        return NULL;
    }

#if ZERO_FIBER_DEBUG
    printf("resuming from %s\n", current_fiber->description);
    printf("  to %s\n", coroutine->description);
#endif

	coroutine->caller = current_fiber;
    coroutine->caller->status = ZERO_FIBER_SUSPENDED;
    coroutine->userdata = userdata;
	coroutine->status = ZERO_FIBER_RUNNING;

    zero_fiber_current = coroutine;

    zero_context_switch(coroutine->context);

    zero_userdata_t returndata = coroutine->caller->userdata;
    return returndata;
}

ZERO_FIBER_API_DECL int zero_fiber_is_active(struct zero_fiber_t *fiber) {
    return ((fiber!=0) && (fiber->status != ZERO_FIBER_ENDED));
}

ZERO_FIBER_API_DECL zero_userdata_t zero_fiber_yield(zero_userdata_t userdata) {
    struct zero_fiber_t *current_fiber = zero_fiber_active();

#if ZERO_FIBER_DEBUG
    printf("yielding from %s\n", current_fiber->description);
#endif

    if(current_fiber->status == ZERO_FIBER_ENDED) {
        return NULL;
    }

#if ZERO_FIBER_DEBUG
    if(current_fiber == &zero_fiber_main) {
        // can't yield from main fiber
        // just exit
        printf("  can't yield from main fiber\n");
        return NULL;
    }

    if(current_fiber->caller == NULL) {
        // can't return to nonexistent caller
        printf("  can't return to nonexistent caller\n");
        return NULL;
    }
#endif

#if ZERO_FIBER_DEBUG
    printf("  to %s\n", current_fiber->caller->description);
#endif

    struct zero_fiber_t *caller = current_fiber->caller;

    current_fiber->caller->userdata = userdata;
    current_fiber->status = ZERO_FIBER_SUSPENDED;
    caller->status = ZERO_FIBER_RUNNING;
    struct zero_fiber_t *previous = current_fiber;

    current_fiber = caller;

    zero_fiber_current = current_fiber;

    zero_context_switch(caller->context);

    zero_userdata_t returndata = previous->userdata;
    return returndata;
}

ZERO_FIBER_API_DECL void zero_fiber_delete(struct zero_fiber_t *fiber) {
    zero_context_delete(fiber->context);
    ZERO_FIBER_FREE(fiber);
}


#endif // ZERO_FIBER_IMPL