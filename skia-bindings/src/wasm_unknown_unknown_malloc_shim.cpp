#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

extern "C" void* skia_bindings_alloc(size_t size, size_t align);
extern "C" void* skia_bindings_alloc_zeroed(size_t size, size_t align);
extern "C" void skia_bindings_dealloc(void* ptr, size_t size, size_t align);

namespace {

constexpr size_t kBaseAlignment = alignof(std::max_align_t);

struct AllocHeader {
    uintptr_t base_ptr;
    size_t total_size;
    size_t alignment;
};

[[noreturn]] void trap_out_of_memory() {
#if defined(__clang__)
    __builtin_trap();
#else
    abort();
#endif
}

bool is_power_of_two(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool checked_add(size_t a, size_t b, size_t* out) {
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

uintptr_t align_up(uintptr_t value, size_t alignment) {
    return (value + (uintptr_t)(alignment - 1)) & ~((uintptr_t)alignment - 1);
}

AllocHeader* header_from_ptr(const void* ptr) {
    return reinterpret_cast<AllocHeader*>(reinterpret_cast<uintptr_t>(ptr) - sizeof(AllocHeader));
}

void* allocate_block(size_t size, size_t alignment, bool zeroed, bool throw_on_failure) {
    const size_t normalized_size = size == 0 ? 1 : size;
    const size_t normalized_alignment = std::max(alignment, kBaseAlignment);

    if (!is_power_of_two(normalized_alignment)) {
        if (throw_on_failure) {
            trap_out_of_memory();
        }
        return nullptr;
    }

    size_t overhead;
    if (!checked_add(sizeof(AllocHeader), normalized_alignment - 1, &overhead)) {
        if (throw_on_failure) {
            trap_out_of_memory();
        }
        return nullptr;
    }

    size_t total_size;
    if (!checked_add(normalized_size, overhead, &total_size)) {
        if (throw_on_failure) {
            trap_out_of_memory();
        }
        return nullptr;
    }

    void* base = zeroed ? skia_bindings_alloc_zeroed(total_size, kBaseAlignment)
                        : skia_bindings_alloc(total_size, kBaseAlignment);
    if (!base) {
        if (throw_on_failure) {
            trap_out_of_memory();
        }
        return nullptr;
    }

    const uintptr_t raw = reinterpret_cast<uintptr_t>(base) + sizeof(AllocHeader);
    const uintptr_t user = align_up(raw, normalized_alignment);
    auto* header = reinterpret_cast<AllocHeader*>(user - sizeof(AllocHeader));
    header->base_ptr = reinterpret_cast<uintptr_t>(base);
    header->total_size = total_size;
    header->alignment = normalized_alignment;

    return reinterpret_cast<void*>(user);
}

void deallocate_block(void* ptr) {
    if (!ptr) {
        return;
    }

    AllocHeader* header = header_from_ptr(ptr);
    skia_bindings_dealloc(reinterpret_cast<void*>(header->base_ptr), header->total_size, kBaseAlignment);
}

}  // namespace

void* operator new(std::size_t size) {
    return allocate_block(size, kBaseAlignment, false, true);
}

void* operator new[](std::size_t size) {
    return allocate_block(size, kBaseAlignment, false, true);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocate_block(size, kBaseAlignment, false, false);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocate_block(size, kBaseAlignment, false, false);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_block(size, static_cast<size_t>(alignment), false, true);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_block(size, static_cast<size_t>(alignment), false, true);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate_block(size, static_cast<size_t>(alignment), false, false);
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate_block(size, static_cast<size_t>(alignment), false, false);
}

void operator delete(void* ptr) noexcept {
    deallocate_block(ptr);
}

void operator delete[](void* ptr) noexcept {
    deallocate_block(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    deallocate_block(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    deallocate_block(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    deallocate_block(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    deallocate_block(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    deallocate_block(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    deallocate_block(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    deallocate_block(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    deallocate_block(ptr);
}

// -----------------------------------------------------------------------------
// C allocator family.
//
// These definitions preempt wasi-libc's dlmalloc. Without them, Skia's
// `malloc`/`free` (SkMemory_malloc.cpp) and libjpeg/libpng/freetype/harfbuzz
// allocations are served by wasi-libc's dlmalloc while `operator new` goes
// through `skia_bindings_alloc` — two independent allocators both carving
// chunks out of the same linear memory starting at `__heap_base`, which
// hands out overlapping memory and corrupts the heap.
// -----------------------------------------------------------------------------
extern "C" {

void* malloc(std::size_t size) {
    return allocate_block(size, kBaseAlignment, false, false);
}

void* calloc(std::size_t n, std::size_t size) {
    std::size_t total;
    if (n != 0 && (size > SIZE_MAX / n)) {
        return nullptr;
    }
    total = n * size;
    return allocate_block(total, kBaseAlignment, true, false);
}

void* realloc(void* ptr, std::size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return nullptr;
    }
    AllocHeader* header = header_from_ptr(ptr);
    std::size_t old_capacity =
        header->total_size - static_cast<std::size_t>(reinterpret_cast<uintptr_t>(ptr) - header->base_ptr);
    void* new_ptr = malloc(size);
    if (new_ptr) {
        std::memcpy(new_ptr, ptr, std::min(size, old_capacity));
        free(ptr);
    }
    return new_ptr;
}

void free(void* ptr) {
    deallocate_block(ptr);
}

void* aligned_alloc(std::size_t alignment, std::size_t size) {
    return allocate_block(size, alignment, false, false);
}

void* memalign(std::size_t alignment, std::size_t size) {
    return allocate_block(size, alignment, false, false);
}

int posix_memalign(void** memptr, std::size_t alignment, std::size_t size) {
    if (!memptr || alignment % sizeof(void*) != 0 || !is_power_of_two(alignment)) {
        return EINVAL;
    }
    void* ptr = allocate_block(size, alignment, false, false);
    if (!ptr) {
        return ENOMEM;
    }
    *memptr = ptr;
    return 0;
}

// Aliases referenced internally by wasi-libc (e.g. atexit machinery); without
// these, libc.a's dlmalloc.c.obj still gets pulled into the link and clashes
// with the definitions above.
void* __libc_malloc(std::size_t size) {
    return malloc(size);
}

void* __libc_calloc(std::size_t n, std::size_t size) {
    return calloc(n, size);
}

void __libc_free(void* ptr) {
    free(ptr);
}

void* valloc(std::size_t size) {
    return allocate_block(size, 4096, false, false);
}

void* pvalloc(std::size_t size) {
    constexpr std::size_t kPageSize = 4096;
    std::size_t rounded = (size + kPageSize - 1) & ~(kPageSize - 1);
    if (size != 0 && rounded < size) {
        return nullptr;
    }
    return allocate_block(size == 0 ? kPageSize : rounded, kPageSize, false, false);
}

void* reallocarray(void* ptr, std::size_t n, std::size_t size) {
    if (n != 0 && size > SIZE_MAX / n) {
        return nullptr;
    }
    return realloc(ptr, n * size);
}

std::size_t malloc_usable_size(void* ptr) {
    if (!ptr) {
        return 0;
    }
    AllocHeader* header = header_from_ptr(ptr);
    return header->total_size - static_cast<std::size_t>(reinterpret_cast<uintptr_t>(ptr) - header->base_ptr);
}

}  // extern "C"
