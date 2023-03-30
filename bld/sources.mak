#
# List of source files relative to $(top_srcdir)/src
#

libupcxx_sources = \
	backend/gasnet/noise_log.cpp \
	backend/gasnet/runtime.cpp   \
	backend/gasnet/upc_link.c    \
	backend/gasnet/watermark.cpp \
	future/core.cpp              \
	atomic.cpp                   \
	barrier.cpp                  \
	broadcast.cpp                \
	ccs.cpp                      \
	copy.cpp                     \
	cuda.cpp                     \
	diagnostic.cpp               \
	digest.cpp                   \
	hip.cpp                      \
	ze.cpp                       \
	os_env.cpp                   \
	persona.cpp                  \
	reduce.cpp                   \
	rget.cpp                     \
	rput.cpp                     \
	segment_allocator.cpp        \
	serialization.cpp            \
	team.cpp                     \
	upcxx.cpp                    \
	vis.cpp                      \
	dl_malloc.c

#
# Any file-specific $(basename)_EXTRA_FLAGS
#
#  example_EXTRA_FLAGS = -Dpi=3.14
