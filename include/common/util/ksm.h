#ifndef EQT_KSM_H
#define EQT_KSM_H

// Standalone Kernel Samepage Merging (KSM) functionality for WillEQ
// Provides page-aligned memory allocation for optimal memory sharing

#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <stdexcept>

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// Page-aligned allocator for std::vector
template <typename T>
class PageAlignedAllocator {
public:
	using value_type = T;

	PageAlignedAllocator() noexcept = default;
	template <typename U>
	PageAlignedAllocator(const PageAlignedAllocator<U>&) noexcept {}

	T* allocate(std::size_t n) {
		void* ptr = nullptr;
		size_t size = n * sizeof(T);

#ifdef _WIN32
		ptr = malloc(size);
		if (!ptr) throw std::bad_alloc();
#else
		size_t alignment = getPageSize();
		if (posix_memalign(&ptr, alignment, size) != 0) {
			throw std::bad_alloc();
		}
#endif
		return static_cast<T*>(ptr);
	}

	void deallocate(T* p, std::size_t) noexcept {
		free(p);
	}

private:
	size_t getPageSize() const {
#ifdef _WIN32
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		return sysInfo.dwPageSize;
#else
		return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
	}
};

template <typename T, typename U>
bool operator==(const PageAlignedAllocator<T>&, const PageAlignedAllocator<U>&) noexcept {
	return true;
}

template <typename T, typename U>
bool operator!=(const PageAlignedAllocator<T>&, const PageAlignedAllocator<U>&) noexcept {
	return false;
}

// KSM namespace with memory management utilities
namespace KSM {

inline size_t getPageSize() {
#ifdef _WIN32
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	return sysInfo.dwPageSize;
#else
	return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}

#ifdef _WIN32
// Windows-specific placeholder functions (no-op)
inline void CheckPageAlignment(void* ptr) {
	(void)ptr;
}

inline void* AllocatePageAligned(size_t size) {
	void* ptr = malloc(size);
	if (ptr) {
		memset(ptr, 0, size);
	}
	return ptr;
}

inline void MarkMemoryForKSM(void* start, size_t size) {
	(void)start;
	(void)size;
}

inline void AlignHeapToPageBoundary() {
}

inline void* MarkHeapStart() {
	return nullptr;
}

inline size_t MeasureHeapUsage(void* start) {
	(void)start;
	return 0;
}

#else
// Linux-specific functionality
inline void CheckPageAlignment(void* ptr) {
	size_t page_size = sysconf(_SC_PAGESIZE);
	(void)page_size;
	(void)ptr;
	// Silently check alignment - no logging in standalone version
}

inline void* AllocatePageAligned(size_t size) {
	size_t page_size = sysconf(_SC_PAGESIZE);
	void* aligned_ptr = nullptr;
	if (posix_memalign(&aligned_ptr, page_size, size) != 0) {
		return nullptr;
	}
	std::memset(aligned_ptr, 0, size);
	return aligned_ptr;
}

inline void MarkMemoryForKSM(void* start, size_t size) {
	// Attempt to mark memory for KSM, ignore errors silently
	madvise(start, size, MADV_MERGEABLE);
}

inline void AlignHeapToPageBoundary() {
	size_t page_size = sysconf(_SC_PAGESIZE);
	if (page_size == 0) {
		return;
	}

	void* current_break = sbrk(0);
	if (current_break == (void*)-1) {
		return;
	}

	uintptr_t current_address = reinterpret_cast<uintptr_t>(current_break);
	size_t misalignment = current_address % page_size;

	if (misalignment != 0) {
		size_t adjustment = page_size - misalignment;
		sbrk(adjustment);
	}
}

inline void* MarkHeapStart() {
	void* current_pos = sbrk(0);
	AlignHeapToPageBoundary();
	return current_pos;
}

inline size_t MeasureHeapUsage(void* start) {
	void* current_break = sbrk(0);
	return static_cast<char*>(current_break) - static_cast<char*>(start);
}
#endif

template <typename T>
inline void PageAlignVectorAligned(std::vector<T, PageAlignedAllocator<T>>& vec) {
	if (vec.empty()) {
		return;
	}

	size_t page_size = getPageSize();
	void* start = vec.data();
	size_t size = vec.size() * sizeof(T);

	if (reinterpret_cast<std::uintptr_t>(start) % page_size != 0) {
		std::vector<T, PageAlignedAllocator<T>> aligned_vec(vec.get_allocator());
		aligned_vec.reserve(vec.capacity());
		aligned_vec.insert(aligned_vec.end(), vec.begin(), vec.end());
		vec.swap(aligned_vec);
		aligned_vec.clear();

		start = vec.data();
		if (reinterpret_cast<std::uintptr_t>(start) % page_size != 0) {
			throw std::runtime_error("Failed to align vector memory to page boundaries.");
		}
	}

#ifndef _WIN32
	MarkMemoryForKSM(start, size);
#else
	(void)size;
#endif
}

} // namespace KSM

#endif // EQT_KSM_H
