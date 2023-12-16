#ifndef _55bb8bd4_a386_4a08_b8fb_93cc31232791
#define _55bb8bd4_a386_4a08_b8fb_93cc31232791

/**
 * exceptions.hpp
 */

#include <upcxx/backend_fwd.hpp>
#include <upcxx/backend/gasnet/noise_log.hpp>

#include <sstream>
#include <cstddef>
#include <new> // bad_alloc

namespace upcxx {
  //////////////////////////////////////////////////////////////////////
  struct bad_shared_alloc : public std::bad_alloc {
    bad_shared_alloc(const char *where=nullptr, size_t nbytes=0, bool showName=true) noexcept {
      std::stringstream ss;
      if (showName) ss << _base;
      ss << "UPC++ shared heap is out of memory on process " << rank_me();
      if (where) ss << "\n inside upcxx::" << where;
      if (nbytes) ss << " while trying to allocate additional " 
                     << nbytes <<  " bytes (" << backend::gasnet::noise_log::size(nbytes) << ")";
      ss << "\n " << detail::shared_heap_stats();
      ss << "\n You may need to request a larger shared heap with `upcxx-run -shared-heap`"
               " or $UPCXX_SHARED_HEAP_SIZE.";
      _what = ss.str();
    }
    bad_shared_alloc(const std::string & reason) noexcept : _what(_base) {
      _what += reason;
    }
    virtual const char* what() const noexcept {
      return _what.c_str();
    }
    private:
     std::string _what;
     static constexpr const char *_base = "upcxx::bad_shared_alloc: ";
  };
  //////////////////////////////////////////////////////////////////////
  struct bad_segment_alloc : public std::bad_alloc {
    bad_segment_alloc(const char *device_typename=nullptr, size_t nbytes=0, intrank_t who=-1) noexcept {
      std::stringstream ss;
      if (!device_typename) device_typename = "Device";
      ss << _base << "UPC++ failed to allocate " << device_typename << " segment memory";
      if (who == -1) ss << " on one or more processes";
      else           ss << " on process " << who << " (and possibly others)";
      ss << "\n inside upcxx::device_allocator<" << device_typename <<"> segment-allocating constructor";
      if (nbytes) ss << "\n while trying to allocate a segment of size " 
                     << nbytes <<  " bytes (" << backend::gasnet::noise_log::size(nbytes) << ")";
      ss << "\n You may need to request a smaller device segment to accommodate the memory capacity of your device.";
      _what = ss.str();
    }
    bad_segment_alloc(const std::string & reason) noexcept : _what(_base) {
      _what += reason;
    }
    virtual const char* what() const noexcept {
      return _what.c_str();
    }
    private:
     std::string _what;
     static constexpr const char *_base = "upcxx::bad_segment_alloc: ";
  };
  //////////////////////////////////////////////////////////////////////
  
} // namespace upcxx

#endif
