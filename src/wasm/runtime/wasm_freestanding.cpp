#include "wasm_freestanding.h"

extern "C" {

    // Symbol provided by the WASM linker pointing to the start of the heap
    extern unsigned char __heap_base;
    static unsigned char* heap_ptr = &__heap_base;

    // Simple sbrk implementation for dlmalloc
    void* sbrk(intptr_t increment) {
        if (increment == 0) return heap_ptr;

        unsigned char* old_heap_ptr = heap_ptr;
        unsigned char* new_heap_ptr = heap_ptr + increment;

        // Current WASM memory size in bytes (page size is 64KB)
        size_t current_size = __builtin_wasm_memory_size(0) * 65536;

        if ((size_t)new_heap_ptr > current_size) {
            // Calculate pages needed
            size_t needed = (size_t)new_heap_ptr - current_size;
            size_t pages = (needed + 65535) / 65536;
            
            // Grow memory
            if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) {
                return (void*)-1; // Out of memory
            }
        }

        heap_ptr = new_heap_ptr;
        return old_heap_ptr;
    }

    void* memset(void* dest, int val, size_t n) {
        unsigned char* ptr = (unsigned char*)dest;
        while (n-- > 0) *ptr++ = (unsigned char)val;
        return dest;
    }

    void* memcpy(void* dest, const void* src, size_t n) {
        unsigned char* d = (unsigned char*)dest;
        const unsigned char* s = (const unsigned char*)src;
        while (n-- > 0) *d++ = *s++;
        return dest;
    }

    void* memmove(void* dest, const void* src, size_t n) {
        unsigned char* d = (unsigned char*)dest;
        const unsigned char* s = (const unsigned char*)src;
        if (d < s) {
            while (n--) *d++ = *s++;
        } else {
            d += n; s += n;
            while (n--) *--d = *--s;
        }
        return dest;
    }

    void abort() {
        __builtin_trap();
    }
}
