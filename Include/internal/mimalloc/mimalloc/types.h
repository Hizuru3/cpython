/* ----------------------------------------------------------------------------
Copyright (c) 2018-2023, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#pragma once
#ifndef MIMALLOC_TYPES_H
#define MIMALLOC_TYPES_H

// --------------------------------------------------------------------------
// This file contains the main type definitions for mimalloc:
// mi_heap_t      : all data for a thread-local heap, contains
//                  lists of all managed heap pages.
// mi_segment_t   : a larger chunk of memory (32GiB) from where pages
//                  are allocated.
// mi_page_t      : a mimalloc page (usually 64KiB or 512KiB) from
//                  where objects are allocated.
// --------------------------------------------------------------------------


#include <stddef.h>   // ptrdiff_t
#include <stdint.h>   // uintptr_t, uint16_t, etc
#include "atomic.h"   // _Atomic

#ifdef _MSC_VER
#pragma warning(disable:4214) // bitfield is not int
#endif

// Minimal alignment necessary. On most platforms 16 bytes are needed
// due to SSE registers for example. This must be at least `sizeof(void*)`
#ifndef MI_MAX_ALIGN_SIZE
#define MI_MAX_ALIGN_SIZE  16   // sizeof(max_align_t)
#endif

#define MI_CACHE_LINE          64
#if defined(_MSC_VER)
#pragma warning(disable:4127)   // suppress constant conditional warning (due to MI_SECURE paths)
#pragma warning(disable:26812)  // unscoped enum warning
#define mi_decl_noinline        __declspec(noinline)
#define mi_decl_thread          __declspec(thread)
#define mi_decl_cache_align     __declspec(align(MI_CACHE_LINE))
#elif (defined(__GNUC__) && (__GNUC__ >= 3)) || defined(__clang__) // includes clang and icc
#define mi_decl_noinline        __attribute__((noinline))
#define mi_decl_thread          __thread
#define mi_decl_cache_align     __attribute__((aligned(MI_CACHE_LINE)))
#else
#define mi_decl_noinline
#define mi_decl_thread          __thread        // hope for the best :-)
#define mi_decl_cache_align
#endif

#if (MI_DEBUG)
#if defined(_MSC_VER)
#define mi_decl_noreturn        __declspec(noreturn)
#elif (defined(__GNUC__) && (__GNUC__ >= 3)) || defined(__clang__)
#define mi_decl_noreturn        __attribute__((__noreturn__))
#else
#define mi_decl_noreturn
#endif

/*
 * 'cold' attribute seems to have been fully supported since GCC 4.x.
 * See https://github.com/gcc-mirror/gcc/commit/52bf96d2f299e9e6.
 */
#if (defined(__GNUC__) && (__GNUC__ >= 4)) || defined(__clang__)
#define mi_decl_cold            __attribute__((cold))
#else
#define mi_decl_cold
#endif

#if (defined(__GNUC__) && defined(__THROW))
#define mi_decl_throw           __THROW
#else
#define mi_decl_throw
#endif
#endif

// ------------------------------------------------------
// Variants
// ------------------------------------------------------

// Define NDEBUG in the release version to disable assertions.
// #define NDEBUG

// Define MI_TRACK_<tool> to enable tracking support
// #define MI_TRACK_VALGRIND 1
// #define MI_TRACK_ASAN     1
// #define MI_TRACK_ETW      1

// Define MI_STAT as 1 to maintain statistics; set it to 2 to have detailed statistics (but costs some performance).
// #define MI_STAT 1

// Define MI_SECURE to enable security mitigations
// #define MI_SECURE 1  // guard page around metadata
// #define MI_SECURE 2  // guard page around each mimalloc page
// #define MI_SECURE 3  // encode free lists (detect corrupted free list (buffer overflow), and invalid pointer free)
// #define MI_SECURE 4  // checks for double free. (may be more expensive)

#if !defined(MI_SECURE)
#define MI_SECURE 0
#endif

// Define MI_DEBUG for debug mode
// #define MI_DEBUG 1  // basic assertion checks and statistics, check double free, corrupted free list, and invalid pointer free.
// #define MI_DEBUG 2  // + internal assertion checks
// #define MI_DEBUG 3  // + extensive internal invariant checking (cmake -DMI_DEBUG_FULL=ON)
#if !defined(MI_DEBUG)
#if !defined(NDEBUG) || defined(_DEBUG)
#define MI_DEBUG 2
#else
#define MI_DEBUG 0
#endif
#endif

// Reserve extra padding at the end of each block to be more resilient against heap block overflows.
// The padding can detect buffer overflow on free.
#if !defined(MI_PADDING) && (MI_SECURE>=3 || MI_DEBUG>=1 || (MI_TRACK_VALGRIND || MI_TRACK_ASAN || MI_TRACK_ETW))
#define MI_PADDING  1
#endif

// Check padding bytes; allows byte-precise buffer overflow detection
#if !defined(MI_PADDING_CHECK) && MI_PADDING && (MI_SECURE>=3 || MI_DEBUG>=1)
#define MI_PADDING_CHECK 1
#endif


// Encoded free lists allow detection of corrupted free lists
// and can detect buffer overflows, modify after free, and double `free`s.
#if (MI_SECURE>=3 || MI_DEBUG>=1)
#define MI_ENCODE_FREELIST  1
#endif


// We used to abandon huge pages but to eagerly deallocate if freed from another thread,
// but that makes it not possible to visit them during a heap walk or include them in a
// `mi_heap_destroy`. We therefore instead reset/decommit the huge blocks if freed from
// another thread so most memory is available until it gets properly freed by the owning thread.
// #define MI_HUGE_PAGE_ABANDON 1


// ------------------------------------------------------
// Platform specific values
// ------------------------------------------------------

// ------------------------------------------------------
// Size of a pointer.
// We assume that `sizeof(void*)==sizeof(intptr_t)`
// and it holds for all platforms we know of.
//
// However, the C standard only requires that:
//  p == (void*)((intptr_t)p))
// but we also need:
//  i == (intptr_t)((void*)i)
// or otherwise one might define an intptr_t type that is larger than a pointer...
// ------------------------------------------------------

#if INTPTR_MAX > INT64_MAX
# define MI_INTPTR_SHIFT (4)  // assume 128-bit  (as on arm CHERI for example)
#elif INTPTR_MAX == INT64_MAX
# define MI_INTPTR_SHIFT (3)
#elif INTPTR_MAX == INT32_MAX
# define MI_INTPTR_SHIFT (2)
#else
#error platform pointers must be 32, 64, or 128 bits
#endif

#if SIZE_MAX == UINT64_MAX
# define MI_SIZE_SHIFT (3)
typedef int64_t  mi_ssize_t;
#elif SIZE_MAX == UINT32_MAX
# define MI_SIZE_SHIFT (2)
typedef int32_t  mi_ssize_t;
#else
#error platform objects must be 32 or 64 bits
#endif

#if (SIZE_MAX/2) > LONG_MAX
# define MI_ZU(x)  x##ULL
# define MI_ZI(x)  x##LL
#else
# define MI_ZU(x)  x##UL
# define MI_ZI(x)  x##L
#endif

#define MI_INTPTR_SIZE  (1<<MI_INTPTR_SHIFT)
#define MI_INTPTR_BITS  (MI_INTPTR_SIZE*8)

#define MI_SIZE_SIZE  (1<<MI_SIZE_SHIFT)
#define MI_SIZE_BITS  (MI_SIZE_SIZE*8)

#define MI_KiB     (MI_ZU(1024))
#define MI_MiB     (MI_KiB*MI_KiB)
#define MI_GiB     (MI_MiB*MI_KiB)


// ------------------------------------------------------
// Main internal data-structures
// ------------------------------------------------------

// Main tuning parameters for segment and page sizes
// Sizes for 64-bit (usually divide by two for 32-bit)
#define MI_SEGMENT_SLICE_SHIFT            (13 + MI_INTPTR_SHIFT)         // 64KiB  (32KiB on 32-bit)

#if MI_INTPTR_SIZE > 4
#define MI_SEGMENT_SHIFT                  ( 9 + MI_SEGMENT_SLICE_SHIFT)  // 32MiB
#else
#define MI_SEGMENT_SHIFT                  ( 7 + MI_SEGMENT_SLICE_SHIFT)  // 4MiB on 32-bit
#endif

#define MI_SMALL_PAGE_SHIFT               (MI_SEGMENT_SLICE_SHIFT)       // 64KiB
#define MI_MEDIUM_PAGE_SHIFT              ( 3 + MI_SMALL_PAGE_SHIFT)     // 512KiB


// Derived constants
#define MI_SEGMENT_SIZE                   (MI_ZU(1)<<MI_SEGMENT_SHIFT)
#define MI_SEGMENT_ALIGN                  MI_SEGMENT_SIZE
#define MI_SEGMENT_MASK                   ((uintptr_t)(MI_SEGMENT_ALIGN - 1))
#define MI_SEGMENT_SLICE_SIZE             (MI_ZU(1)<< MI_SEGMENT_SLICE_SHIFT)
#define MI_SLICES_PER_SEGMENT             (MI_SEGMENT_SIZE / MI_SEGMENT_SLICE_SIZE) // 1024

#define MI_SMALL_PAGE_SIZE                (MI_ZU(1)<<MI_SMALL_PAGE_SHIFT)
#define MI_MEDIUM_PAGE_SIZE               (MI_ZU(1)<<MI_MEDIUM_PAGE_SHIFT)

#define MI_SMALL_OBJ_SIZE_MAX             (MI_SMALL_PAGE_SIZE/4)   // 8KiB on 64-bit
#define MI_MEDIUM_OBJ_SIZE_MAX            (MI_MEDIUM_PAGE_SIZE/4)  // 128KiB on 64-bit
#define MI_MEDIUM_OBJ_WSIZE_MAX           (MI_MEDIUM_OBJ_SIZE_MAX/MI_INTPTR_SIZE)
#define MI_LARGE_OBJ_SIZE_MAX             (MI_SEGMENT_SIZE/2)      // 32MiB on 64-bit
#define MI_LARGE_OBJ_WSIZE_MAX            (MI_LARGE_OBJ_SIZE_MAX/MI_INTPTR_SIZE)

// Maximum number of size classes. (spaced exponentially in 12.5% increments)
#define MI_BIN_HUGE  (73U)

#if (MI_MEDIUM_OBJ_WSIZE_MAX >= 655360)
#error "mimalloc internal: define more bins"
#endif

// Maximum slice offset (15)
#define MI_MAX_SLICE_OFFSET               ((MI_ALIGNMENT_MAX / MI_SEGMENT_SLICE_SIZE) - 1)

// Used as a special value to encode block sizes in 32 bits.
#define MI_HUGE_BLOCK_SIZE                ((uint32_t)(2*MI_GiB))

// blocks up to this size are always allocated aligned
#define MI_MAX_ALIGN_GUARANTEE            (8*MI_MAX_ALIGN_SIZE)

// Alignments over MI_ALIGNMENT_MAX are allocated in dedicated huge page segments
#define MI_ALIGNMENT_MAX                  (MI_SEGMENT_SIZE >> 1)


// ------------------------------------------------------
// Mimalloc pages contain allocated blocks
// ------------------------------------------------------

// The free lists use encoded next fields
// (Only actually encodes when MI_ENCODED_FREELIST is defined.)
typedef uintptr_t  mi_encoded_t;

// thread id's
typedef size_t     mi_threadid_t;

// free lists contain blocks
typedef struct mi_block_s {
  _Atomic(mi_encoded_t) next;
} mi_block_t;


// The delayed flags are used for efficient multi-threaded free-ing
typedef enum mi_delayed_e {
  MI_USE_DELAYED_FREE   = 0, // push on the owning heap thread delayed list
  MI_DELAYED_FREEING    = 1, // temporary: another thread is accessing the owning heap
  MI_NO_DELAYED_FREE    = 2, // optimize: push on page local thread free queue if another block is already in the heap thread delayed free list
  MI_NEVER_DELAYED_FREE = 3  // sticky, only resets on page reclaim
} mi_delayed_t;


// The `in_full` and `has_aligned` page flags are put in a union to efficiently
// test if both are false (`full_aligned == 0`) in the `mi_free` routine.
#if !MI_TSAN
typedef union mi_page_flags_s {
  uint8_t full_aligned;
  struct {
    uint8_t in_full : 1;
    uint8_t has_aligned : 1;
  } x;
} mi_page_flags_t;
#else
// under thread sanitizer, use a byte for each flag to suppress warning, issue #130
typedef union mi_page_flags_s {
  uint16_t full_aligned;
  struct {
    uint8_t in_full;
    uint8_t has_aligned;
  } x;
} mi_page_flags_t;
#endif

// Thread free list.
// We use the bottom 2 bits of the pointer for mi_delayed_t flags
typedef uintptr_t mi_thread_free_t;

// A page contains blocks of one specific size (`block_size`).
// Each page has three list of free blocks:
// `free` for blocks that can be allocated,
// `local_free` for freed blocks that are not yet available to `mi_malloc`
// `thread_free` for freed blocks by other threads
// The `local_free` and `thread_free` lists are migrated to the `free` list
// when it is exhausted. The separate `local_free` list is necessary to
// implement a monotonic heartbeat. The `thread_free` list is needed for
// avoiding atomic operations in the common case.
//
//
// `used - |thread_free|` == actual blocks that are in use (alive)
// `used - |thread_free| + |free| + |local_free| == capacity`
//
// We don't count `freed` (as |free|) but use `used` to reduce
// the number of memory accesses in the `mi_page_all_free` function(s).
//
// Notes:
// - Access is optimized for `mi_free` and `mi_page_alloc` (in `alloc.c`)
// - Using `uint16_t` does not seem to slow things down
// - The size is 8 words on 64-bit which helps the page index calculations
//   (and 10 words on 32-bit, and encoded free lists add 2 words. Sizes 10
//    and 12 are still good for address calculation)
// - To limit the structure size, the `xblock_size` is 32-bits only; for
//   blocks > MI_HUGE_BLOCK_SIZE the size is determined from the segment page size
// - `thread_free` uses the bottom bits as a delayed-free flags to optimize
//   concurrent frees where only the first concurrent free adds to the owning
//   heap `thread_delayed_free` list (see `alloc.c:mi_free_block_mt`).
//   The invariant is that no-delayed-free is only set if there is
//   at least one block that will be added, or as already been added, to
//   the owning heap `thread_delayed_free` list. This guarantees that pages
//   will be freed correctly even if only other threads free blocks.
typedef struct mi_page_s {
  // "owned" by the segment
  uint32_t              slice_count;       // slices in this page (0 if not a page)
  uint32_t              slice_offset;      // distance from the actual page data slice (0 if a page)
  uint8_t               is_committed : 1;  // `true` if the page virtual memory is committed
  uint8_t               is_zero_init : 1;  // `true` if the page was initially zero initialized
  uint8_t               use_qsbr : 1;      // delay page freeing using qsbr
  uint8_t               tag : 4;           // tag from the owning heap
  uint8_t               debug_offset;      // number of bytes to preserve when filling freed or uninitialized memory

  // layout like this to optimize access in `mi_malloc` and `mi_free`
  uint16_t              capacity;          // number of blocks committed, must be the first field, see `segment.c:page_clear`
  uint16_t              reserved;          // number of blocks reserved in memory
  mi_page_flags_t       flags;             // `in_full` and `has_aligned` flags (8 bits)
  uint8_t               free_is_zero : 1;  // `true` if the blocks in the free list are zero initialized
  uint8_t               retire_expire : 7; // expiration count for retired blocks

  mi_block_t*           free;              // list of available free blocks (`malloc` allocates from this list)
  uint32_t              used;              // number of blocks in use (including blocks in `local_free` and `thread_free`)
  uint32_t              xblock_size;       // size available in each block (always `>0`)
  mi_block_t*           local_free;        // list of deferred free blocks by this thread (migrates to `free`)

  #if (MI_ENCODE_FREELIST || MI_PADDING)
  uintptr_t             keys[2];           // two random keys to encode the free lists (see `_mi_block_next`) or padding canary
  #endif

  _Atomic(mi_thread_free_t) xthread_free;  // list of deferred free blocks freed by other threads
  _Atomic(uintptr_t)        xheap;

  struct mi_page_s*     next;              // next page owned by this thread with the same `block_size`
  struct mi_page_s*     prev;              // previous page owned by this thread with the same `block_size`

#ifdef Py_GIL_DISABLED
  struct llist_node     qsbr_node;
  uint64_t              qsbr_goal;
#endif

  // 64-bit 9 words, 32-bit 12 words, (+2 for secure)
  #if MI_INTPTR_SIZE==8 && !defined(Py_GIL_DISABLED)
  uintptr_t padding[1];
  #endif
} mi_page_t;



// ------------------------------------------------------
// Mimalloc segments contain mimalloc pages
// ------------------------------------------------------

typedef enum mi_page_kind_e {
  MI_PAGE_SMALL,    // small blocks go into 64KiB pages inside a segment
  MI_PAGE_MEDIUM,   // medium blocks go into medium pages inside a segment
  MI_PAGE_LARGE,    // larger blocks go into a page of just one block
  MI_PAGE_HUGE,     // huge blocks (> 16 MiB) are put into a single page in a single segment.
} mi_page_kind_t;

typedef enum mi_segment_kind_e {
  MI_SEGMENT_NORMAL, // MI_SEGMENT_SIZE size with pages inside.
  MI_SEGMENT_HUGE,   // > MI_LARGE_SIZE_MAX segment with just one huge page inside.
} mi_segment_kind_t;

// ------------------------------------------------------
// A segment holds a commit mask where a bit is set if
// the corresponding MI_COMMIT_SIZE area is committed.
// The MI_COMMIT_SIZE must be a multiple of the slice
// size. If it is equal we have the most fine grained
// decommit (but setting it higher can be more efficient).
// The MI_MINIMAL_COMMIT_SIZE is the minimal amount that will
// be committed in one go which can be set higher than
// MI_COMMIT_SIZE for efficiency (while the decommit mask
// is still tracked in fine-grained MI_COMMIT_SIZE chunks)
// ------------------------------------------------------

#define MI_MINIMAL_COMMIT_SIZE      (1*MI_SEGMENT_SLICE_SIZE)
#define MI_COMMIT_SIZE              (MI_SEGMENT_SLICE_SIZE)              // 64KiB
#define MI_COMMIT_MASK_BITS         (MI_SEGMENT_SIZE / MI_COMMIT_SIZE)
#define MI_COMMIT_MASK_FIELD_BITS    MI_SIZE_BITS
#define MI_COMMIT_MASK_FIELD_COUNT  (MI_COMMIT_MASK_BITS / MI_COMMIT_MASK_FIELD_BITS)

#if (MI_COMMIT_MASK_BITS != (MI_COMMIT_MASK_FIELD_COUNT * MI_COMMIT_MASK_FIELD_BITS))
#error "the segment size must be exactly divisible by the (commit size * size_t bits)"
#endif

typedef struct mi_commit_mask_s {
  size_t mask[MI_COMMIT_MASK_FIELD_COUNT];
} mi_commit_mask_t;

typedef mi_page_t  mi_slice_t;
typedef int64_t    mi_msecs_t;


// Memory can reside in arena's, direct OS allocated, or statically allocated. The memid keeps track of this.
typedef enum mi_memkind_e {
  MI_MEM_NONE,      // not allocated
  MI_MEM_EXTERNAL,  // not owned by mimalloc but provided externally (via `mi_manage_os_memory` for example)
  MI_MEM_STATIC,    // allocated in a static area and should not be freed (for arena meta data for example)
  MI_MEM_OS,        // allocated from the OS
  MI_MEM_OS_HUGE,   // allocated as huge os pages
  MI_MEM_OS_REMAP,  // allocated in a remapable area (i.e. using `mremap`)
  MI_MEM_ARENA      // allocated from an arena (the usual case)
} mi_memkind_t;

static inline bool mi_memkind_is_os(mi_memkind_t memkind) {
  return (memkind >= MI_MEM_OS && memkind <= MI_MEM_OS_REMAP);
}

typedef struct mi_memid_os_info {
  void*         base;               // actual base address of the block (used for offset aligned allocations)
  size_t        alignment;          // alignment at allocation
} mi_memid_os_info_t;

typedef struct mi_memid_arena_info {
  size_t        block_index;        // index in the arena
  mi_arena_id_t id;                 // arena id (>= 1)
  bool          is_exclusive;       // the arena can only be used for specific arena allocations
} mi_memid_arena_info_t;

typedef struct mi_memid_s {
  union {
    mi_memid_os_info_t    os;       // only used for MI_MEM_OS
    mi_memid_arena_info_t arena;    // only used for MI_MEM_ARENA
  } mem;
  bool          is_pinned;          // `true` if we cannot decommit/reset/protect in this memory (e.g. when allocated using large OS pages)
  bool          initially_committed;// `true` if the memory was originally allocated as committed
  bool          initially_zero;     // `true` if the memory was originally zero initialized
  mi_memkind_t  memkind;
} mi_memid_t;


// Segments are large allocated memory blocks (8mb on 64 bit) from
// the OS. Inside segments we allocated fixed size _pages_ that
// contain blocks.
typedef struct mi_segment_s {
  // constant fields
  mi_memid_t        memid;              // memory id for arena allocation
  bool              allow_decommit;
  bool              allow_purge;
  size_t            segment_size;

  // segment fields
  mi_msecs_t        purge_expire;
  mi_commit_mask_t  purge_mask;
  mi_commit_mask_t  commit_mask;

  _Atomic(struct mi_segment_s*) abandoned_next;

  // from here is zero initialized
  struct mi_segment_s* next;            // the list of freed segments in the cache (must be first field, see `segment.c:mi_segment_init`)

  size_t            abandoned;          // abandoned pages (i.e. the original owning thread stopped) (`abandoned <= used`)
  size_t            abandoned_visits;   // count how often this segment is visited in the abandoned list (to force reclaim if it is too long)
  size_t            used;               // count of pages in use
  uintptr_t         cookie;             // verify addresses in debug mode: `mi_ptr_cookie(segment) == segment->cookie`

  size_t            segment_slices;      // for huge segments this may be different from `MI_SLICES_PER_SEGMENT`
  size_t            segment_info_slices; // initial slices we are using segment info and possible guard pages.

  // layout like this to optimize access in `mi_free`
  mi_segment_kind_t kind;
  size_t            slice_entries;       // entries in the `slices` array, at most `MI_SLICES_PER_SEGMENT`
  _Atomic(mi_threadid_t) thread_id;      // unique id of the thread owning this segment

  mi_slice_t        slices[MI_SLICES_PER_SEGMENT+1];  // one more for huge blocks with large alignment
} mi_segment_t;

typedef uintptr_t        mi_tagged_segment_t;

// Segments unowned by any thread are put in a shared pool
typedef struct mi_abandoned_pool_s {
  // This is a list of visited abandoned pages that were full at the time.
  // this list migrates to `abandoned` when that becomes NULL. The use of
  // this list reduces contention and the rate at which segments are visited.
  mi_decl_cache_align _Atomic(mi_segment_t*)       abandoned_visited; // = NULL

  // The abandoned page list (tagged as it supports pop)
  mi_decl_cache_align _Atomic(mi_tagged_segment_t) abandoned;         // = NULL

  // Maintain these for debug purposes (these counts may be a bit off)
  mi_decl_cache_align _Atomic(size_t)           abandoned_count;
  mi_decl_cache_align _Atomic(size_t)           abandoned_visited_count;

  // We also maintain a count of current readers of the abandoned list
  // in order to prevent resetting/decommitting segment memory if it might
  // still be read.
  mi_decl_cache_align _Atomic(size_t)           abandoned_readers; // = 0
} mi_abandoned_pool_t;


// ------------------------------------------------------
// Heaps
// Provide first-class heaps to allocate from.
// A heap just owns a set of pages for allocation and
// can only be allocate/reallocate from the thread that created it.
// Freeing blocks can be done from any thread though.
// Per thread, the segments are shared among its heaps.
// Per thread, there is always a default heap that is
// used for allocation; it is initialized to statically
// point to an empty heap to avoid initialization checks
// in the fast path.
// ------------------------------------------------------

// Thread local data
typedef struct mi_tld_s mi_tld_t;

// Pages of a certain block size are held in a queue.
typedef struct mi_page_queue_s {
  mi_page_t* first;
  mi_page_t* last;
  size_t     block_size;
} mi_page_queue_t;

#define MI_BIN_FULL  (MI_BIN_HUGE+1)

// Random context
typedef struct mi_random_cxt_s {
  uint32_t input[16];
  uint32_t output[16];
  int      output_available;
  bool     weak;
} mi_random_ctx_t;


// In debug mode there is a padding structure at the end of the blocks to check for buffer overflows
#if (MI_PADDING)
typedef struct mi_padding_s {
  uint32_t canary; // encoded block value to check validity of the padding (in case of overflow)
  uint32_t delta;  // padding bytes before the block. (mi_usable_size(p) - delta == exact allocated bytes)
} mi_padding_t;
#define MI_PADDING_SIZE   (sizeof(mi_padding_t))
#define MI_PADDING_WSIZE  ((MI_PADDING_SIZE + MI_INTPTR_SIZE - 1) / MI_INTPTR_SIZE)
#else
#define MI_PADDING_SIZE   0
#define MI_PADDING_WSIZE  0
#endif

#define MI_PAGES_DIRECT   (MI_SMALL_WSIZE_MAX + MI_PADDING_WSIZE + 1)


// A heap owns a set of pages.
struct mi_heap_s {
  mi_tld_t*             tld;
  mi_page_t*            pages_free_direct[MI_PAGES_DIRECT];  // optimize: array where every entry points a page with possibly free blocks in the corresponding queue for that size.
  mi_page_queue_t       pages[MI_BIN_FULL + 1];              // queue of pages for each size class (or "bin")
  _Atomic(mi_block_t*)  thread_delayed_free;
  mi_threadid_t         thread_id;                           // thread this heap belongs too
  mi_arena_id_t         arena_id;                            // arena id if the heap belongs to a specific arena (or 0)
  uintptr_t             cookie;                              // random cookie to verify pointers (see `_mi_ptr_cookie`)
  uintptr_t             keys[2];                             // two random keys used to encode the `thread_delayed_free` list
  mi_random_ctx_t       random;                              // random number context used for secure allocation
  size_t                page_count;                          // total number of pages in the `pages` queues.
  size_t                page_retired_min;                    // smallest retired index (retired pages are fully free, but still in the page queues)
  size_t                page_retired_max;                    // largest retired index into the `pages` array.
  mi_heap_t*            next;                                // list of heaps per thread
  bool                  no_reclaim;                          // `true` if this heap should not reclaim abandoned pages
  uint8_t               tag;                                 // custom identifier for this heap
  uint8_t               debug_offset;                        // number of bytes to preserve when filling freed or uninitialized memory
  bool                  page_use_qsbr;                       // should freeing pages be delayed using QSBR
};



// ------------------------------------------------------
// Debug
// ------------------------------------------------------

#if !defined(MI_DEBUG_UNINIT)
#define MI_DEBUG_UNINIT     (0xD0)
#endif
#if !defined(MI_DEBUG_FREED)
#define MI_DEBUG_FREED      (0xDF)
#endif
#if !defined(MI_DEBUG_PADDING)
#define MI_DEBUG_PADDING    (0xDE)
#endif

#if (MI_DEBUG)
// use our own assertion to print without memory allocation
mi_decl_noreturn mi_decl_cold mi_decl_throw
void _mi_assert_fail(const char* assertion, const char* fname, unsigned int line, const char* func);
#define mi_assert(expr)     ((expr) ? (void)0 : _mi_assert_fail(#expr,__FILE__,__LINE__,__func__))
#else
#define mi_assert(x)
#endif

#if (MI_DEBUG>1)
#define mi_assert_internal    mi_assert
#else
#define mi_assert_internal(x)
#endif

#if (MI_DEBUG>2)
#define mi_assert_expensive   mi_assert
#else
#define mi_assert_expensive(x)
#endif

// ------------------------------------------------------
// Statistics
// ------------------------------------------------------

#ifndef MI_STAT
#if (MI_DEBUG>0)
#define MI_STAT 2
#else
#define MI_STAT 0
#endif
#endif

typedef struct mi_stat_count_s {
  int64_t allocated;
  int64_t freed;
  int64_t peak;
  int64_t current;
} mi_stat_count_t;

typedef struct mi_stat_counter_s {
  int64_t total;
  int64_t count;
} mi_stat_counter_t;

typedef struct mi_stats_s {
  mi_stat_count_t segments;
  mi_stat_count_t pages;
  mi_stat_count_t reserved;
  mi_stat_count_t committed;
  mi_stat_count_t reset;
  mi_stat_count_t purged;
  mi_stat_count_t page_committed;
  mi_stat_count_t segments_abandoned;
  mi_stat_count_t pages_abandoned;
  mi_stat_count_t threads;
  mi_stat_count_t normal;
  mi_stat_count_t huge;
  mi_stat_count_t large;
  mi_stat_count_t malloc;
  mi_stat_count_t segments_cache;
  mi_stat_counter_t pages_extended;
  mi_stat_counter_t mmap_calls;
  mi_stat_counter_t commit_calls;
  mi_stat_counter_t reset_calls;
  mi_stat_counter_t purge_calls;
  mi_stat_counter_t page_no_retire;
  mi_stat_counter_t searches;
  mi_stat_counter_t normal_count;
  mi_stat_counter_t huge_count;
  mi_stat_counter_t large_count;
#if MI_STAT>1
  mi_stat_count_t normal_bins[MI_BIN_HUGE+1];
#endif
} mi_stats_t;


void _mi_stat_increase(mi_stat_count_t* stat, size_t amount);
void _mi_stat_decrease(mi_stat_count_t* stat, size_t amount);
void _mi_stat_counter_increase(mi_stat_counter_t* stat, size_t amount);

#if (MI_STAT)
#define mi_stat_increase(stat,amount)         _mi_stat_increase( &(stat), amount)
#define mi_stat_decrease(stat,amount)         _mi_stat_decrease( &(stat), amount)
#define mi_stat_counter_increase(stat,amount) _mi_stat_counter_increase( &(stat), amount)
#else
#define mi_stat_increase(stat,amount)         (void)0
#define mi_stat_decrease(stat,amount)         (void)0
#define mi_stat_counter_increase(stat,amount) (void)0
#endif

#define mi_heap_stat_counter_increase(heap,stat,amount)  mi_stat_counter_increase( (heap)->tld->stats.stat, amount)
#define mi_heap_stat_increase(heap,stat,amount)  mi_stat_increase( (heap)->tld->stats.stat, amount)
#define mi_heap_stat_decrease(heap,stat,amount)  mi_stat_decrease( (heap)->tld->stats.stat, amount)

// ------------------------------------------------------
// Thread Local data
// ------------------------------------------------------

// A "span" is an available range of slices. The span queues keep
// track of slice spans of at most the given `slice_count` (but more than the previous size class).
typedef struct mi_span_queue_s {
  mi_slice_t* first;
  mi_slice_t* last;
  size_t      slice_count;
} mi_span_queue_t;

#define MI_SEGMENT_BIN_MAX (35)     // 35 == mi_segment_bin(MI_SLICES_PER_SEGMENT)

// OS thread local data
typedef struct mi_os_tld_s {
  size_t                region_idx;   // start point for next allocation
  mi_stats_t*           stats;        // points to tld stats
} mi_os_tld_t;


// Segments thread local data
typedef struct mi_segments_tld_s {
  mi_span_queue_t     spans[MI_SEGMENT_BIN_MAX+1];  // free slice spans inside segments
  size_t              count;        // current number of segments;
  size_t              peak_count;   // peak number of segments
  size_t              current_size; // current size of all segments
  size_t              peak_size;    // peak size of all segments
  mi_stats_t*         stats;        // points to tld stats
  mi_os_tld_t*        os;           // points to os stats
  mi_abandoned_pool_t* abandoned;   // pool of abandoned segments
} mi_segments_tld_t;

// Thread local data
struct mi_tld_s {
  unsigned long long  heartbeat;     // monotonic heartbeat count
  bool                recurse;       // true if deferred was called; used to prevent infinite recursion.
  mi_heap_t*          heap_backing;  // backing heap of this thread (cannot be deleted)
  mi_heap_t*          heaps;         // list of heaps in this thread (so we can abandon all when the thread terminates)
  mi_segments_tld_t   segments;      // segment tld
  mi_os_tld_t         os;            // os tld
  mi_stats_t          stats;         // statistics
};

#endif
