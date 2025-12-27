#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Standard type definitions for WASM (32-bit)
#ifndef __SIZE_TYPE__
#define __SIZE_TYPE__ unsigned long
#endif
#ifndef __PTRDIFF_TYPE__
#define __PTRDIFF_TYPE__ long
#endif
#ifndef __INTPTR_TYPE__
#define __INTPTR_TYPE__ long
#endif
#ifndef __UINTPTR_TYPE__
#define __UINTPTR_TYPE__ unsigned long
#endif

// IDE helpers: silence errors for compiler extensions when not building for WASM
#ifndef __wasm__
#ifndef __attribute__
#define __attribute__(x)
#endif
#define __builtin_wasm_memory_size(x) 0
#define __builtin_wasm_memory_grow(x, y) (size_t)-1
#define __builtin_trap()
#endif

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

// Definition required by dlmalloc
typedef struct {
    long long __max_align_ll __attribute__((__aligned__(8)));
    double __max_align_ld __attribute__((__aligned__(8)));
} max_align_t;

// Definitions required by dlmalloc in freestanding mode
#define __WASI_ERRNO_NOMEM 12
#define __WASI_ERRNO_INVAL 28
#define emscripten_trace_record_allocation(...)
#define emscripten_trace_record_free(...)
#define emscripten_trace_record_reallocation(...)

// Definitions required by musl math sources
#define hidden __attribute__((__visibility__("hidden")))
#define weak __attribute__((__weak__))

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);
void abort(void);
void* sbrk(intptr_t increment);

#ifdef __cplusplus
}
#endif
