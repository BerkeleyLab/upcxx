#ifndef _485f7f27_ce8a_4829_a04c_aaa8182adab9
#define _485f7f27_ce8a_4829_a04c_aaa8182adab9

// Added for upcxx:
#define ONLY_MSPACES 1
#if UPCXXI_ASSERT_ENABLED
  #define DEBUG 1
#else
  #undef DEBUG
#endif

// Added for upcxx:
#define ONLY_MSPACES 1

/*
 * Added for upcxx. This block of defines name shifts dlmalloc functions to have
 * a upcxxi_ prefix. Since dlmalloc is a commonly used library, name clashes can
 * occur when two libraries that use dlmalloc are linked to the same application
 * causing linker errors as they both define the dlmalloc symbols.
 */
#define create_mspace upcxxi_create_mspace
#define create_mspace_with_base upcxxi_create_mspace_with_base
#define destroy_mspace upcxxi_destroy_mspace
#define mspace_bulk_free upcxxi_mspace_bulk_free
#define mspace_calloc upcxxi_mspace_calloc
#define mspace_footprint upcxxi_mspace_footprint
#define mspace_footprint_limit upcxxi_mspace_footprint_limit
#define mspace_free upcxxi_mspace_free
#define mspace_independent_calloc upcxxi_mspace_independent_calloc
#define mspace_independent_comalloc upcxxi_mspace_independent_comalloc
#define mspace_mallinfo upcxxi_mspace_mallinfo
#define mspace_malloc upcxxi_mspace_malloc
#define mspace_malloc_stats upcxxi_mspace_malloc_stats
#define mspace_mallopt upcxxi_mspace_mallopt
#define mspace_max_footprint upcxxi_mspace_max_footprint
#define mspace_memalign upcxxi_mspace_memalign
#define mspace_realloc upcxxi_mspace_realloc
#define mspace_realloc_in_place upcxxi_mspace_realloc_in_place
#define mspace_set_footprint_limit upcxxi_mspace_set_footprint_limit
#define mspace_track_large_chunks upcxxi_mspace_track_large_chunks
#define mspace_trim upcxxi_mspace_trim
#define mspace_usable_size upcxxi_mspace_usable_size

#endif
