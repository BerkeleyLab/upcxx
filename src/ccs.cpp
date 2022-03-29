#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ostream>
#include <limits>
#include <cstring>
#include <cstddef>
#include <tuple>
#include <iomanip>
#include <cinttypes>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <upcxx/ccs.hpp>
#include <upcxx/team.hpp>
#include <upcxx/reduce.hpp>
#include <upcxx/broadcast.hpp>
#include <upcxx/os_env.hpp>

#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
  #include <dlfcn.h>
  #if UPCXXI_EXEFORMAT_ELF
    #include <link.h>
  #elif UPCXXI_EXEFORMAT_MACHO
    #include <mach/mach.h>
    #include <mach/task_info.h>
    #include <mach-o/dyld_images.h>
    #include <mach-o/dyld.h>
  #else
    #error "Multi-segment relocations are not available for this operating system. Configure with --enable-ccs-rpc to build without segment mapping support."
  #endif
#endif

#if UPCXXI_HAVE___CXA_DEMANGLE
#include <cxxabi.h>
#endif

#define ALIGN_UP(val,align)     (((val) + (align) - 1) & ~((align) -1))

using upcxx::detail::fnv128;

namespace upcxx {
namespace detail {

  void write_helper(int fd, const char* buf, size_t size)
  {
    ssize_t count = 0;
    size_t pos = 0;
    do {
      count = write(fd, buf+pos, size);
      pos += count;
      size -= count;
    } while (count > 0);
    fsync(fd);
  }

#if UPCXXI_EXEFORMAT_MACHO
  dyld_all_image_infos* macho_all_infos()
  {
    task_t task = mach_task_self();
    task_dyld_info dyld_info;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    kern_return_t rc = task_info(task, TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&dyld_info), &count);
    if (rc == KERN_SUCCESS)
    {
      return reinterpret_cast<dyld_all_image_infos*>(dyld_info.all_image_info_addr);
    }
    UPCXXI_FATAL_ERROR("Could not get Mach-O info: " << std::to_string(rc));
    return nullptr;
  }
#elif UPCXXI_EXEFORMAT_ELF
  /*
   * d_un.d_ptr has an annoying property in that the pointer is sometimes already relocated and sometimes
   * still needs relocation performed when processing the values from running program headers. On Linux,
   * it appears that the runtime linker always performs these relocations, leaving the exception of the
   * vDSO, which is injected by the kernel and leaves this pointer unrelocated. It is possible to check
   * for the vDSO with getauxval(AT_SYSINFO_EHDR), however the documentation doesn't specify that this is
   * the only time relocations haven't occurred. It also isn't possible to determine relocation based on
   * if the pointer is less than its basis, as the unrelocated pointer may be in the kernel's address, in
   * the upper limits of the pointer's range, with an address value much higher rather than lower than the
   * range pointers are usually mapped to.
   *
   * The most reliable way of checking to see if this pointer has been relocated or not seems to be to see
   * if it fits within the memory range the DSO was mapped to.
   */
  template<typename F>
  typename std::enable_if<std::is_pointer<F>::value,F>::type
  convert_dptr(ElfW(Addr) dso_lo, ElfW(Addr) dso_hi, ElfW(Addr) basis, ElfW(Addr) ptr)
  {
    if (ptr > dso_lo && ptr < dso_hi)
      return reinterpret_cast<F>(ptr);
    else {
      uintptr_t rptr = ptr + basis;
      if (rptr > dso_lo && rptr < dso_hi) 
        return reinterpret_cast<F>(rptr);
      else {
        /*
         * This doesn't need to be fatal here. The functionality this relocated pointer
         * would provide could be optional.
         */ 
        fprintf(stderr, "Warning: reocation of pointer failed.\n");
        return reinterpret_cast<F>(NULL);
      }
    }
  }
#endif

  const char* segmap_cache::get_symbol(uintptr_t uptr)
  {
#if UPCXXI_EXEFORMAT_ELF
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto& segmap = segmap_cache::segment_map();
    for (const auto& seg : segmap)
    {
      if (uptr >= seg.start && uptr < seg.end)
      {
        const uintptr_t uptr2 = uptr - seg.basis;
        for (auto sym = static_cast<const ElfW(Sym)*>(seg.symtbl); sym < static_cast<const ElfW(Sym)*>(seg.symtblend); ++sym)
        {
          if (uptr2 >= sym->st_value && uptr2 < (sym->st_value + sym->st_size) &&
             ((ELF64_ST_BIND (sym->st_info) == STB_GLOBAL) || (ELF64_ST_BIND (sym->st_info) == STB_WEAK)))
          {
            return (seg.strtbl + sym->st_name);
          }
        }
      }
    }
#else
#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
    void* ptr = reinterpret_cast<void*>(uptr);
    Dl_info info{};
    dladdr(ptr, &info);
    return info.dli_sname;
#endif
#endif
    return nullptr;
  }

#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
#if UPCXXI_EXEFORMAT_ELF
  std::vector<segment_info> segmap_cache::build_segment_map()
  {
    std::vector<segment_info> map;
    size_t phdr_num = 0;
    auto tdata = std::tie(map,phdr_num);
    dl_iterate_phdr([](dl_phdr_info *info, size_t size, void* data)
    {
      auto tup = *static_cast<std::tuple<std::vector<segment_info>&,size_t&>*>(data);
      std::vector<segment_info>& map = std::get<0>(tup);
      size_t& phdr_num = std::get<1>(tup);

      //if (phdr_num == 1) return 0; // skip vDSO

      std::size_t orig_elements = map.size();
      bool has_textrel = false;
      const uint32_t *hashtbl = nullptr;
      const uint32_t *gnuhashtbl = nullptr;
      uint32_t symbol_count = 0;
      const ElfW(Sym)* symtbl = nullptr;
      const ElfW(Sym)* symtblend = nullptr;
      const char* strtbl = nullptr;
      const ElfW(Nhdr) *note = nullptr;
      const ElfW(Nhdr) *note_end = nullptr;
      bool has_build_id = false;
      std::array<uint8_t,elf_hash_size> build_id;
      int flags = 0;
      ElfW(Addr) dptr_basis = 0;
      ElfW(Addr) addr_lo = 0, addr_hi = 0;
      bool found_first = false;

      // If dptr needs to be relocated, it will be based on the first PT_LOAD
      for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD)
        {
          if (!found_first) {
            dptr_basis = info->dlpi_addr;
            addr_lo = info->dlpi_addr + phdr.p_vaddr;
            found_first = true;
          }
          uintptr_t addr_new = info->dlpi_addr + phdr.p_vaddr + phdr.p_memsz;
          if (addr_new > addr_hi) addr_hi = addr_new;
        }
      }

      for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        switch (phdr.p_type)
        {
        case PT_DYNAMIC:
        {
          for (ElfW(Dyn) *dyn = reinterpret_cast<ElfW(Dyn)*>(info->dlpi_addr + phdr.p_vaddr); dyn->d_tag != DT_NULL; ++dyn)
          {
            switch (dyn->d_tag)
            {
            case DT_TEXTREL:
              /*
               * TEXTRELs cause the .text section to have its addresses rewritten
               * to perform relocation. This causes .text to differ between processes
               * and therefore we cannot use its hash.
               */
              has_textrel = true;
              break;
            case DT_HASH:
              hashtbl = convert_dptr<const uint32_t*>(addr_lo, addr_hi, dptr_basis, dyn->d_un.d_ptr);
              break;
            case DT_GNU_HASH:
              gnuhashtbl = convert_dptr<const uint32_t*>(addr_lo, addr_hi, dptr_basis, dyn->d_un.d_ptr);
              break;
            case DT_SYMTAB:
              symtbl = convert_dptr<const ElfW(Sym)*>(addr_lo, addr_hi, dptr_basis, dyn->d_un.d_ptr);
              break;
            case DT_STRTAB:
              strtbl = convert_dptr<const char*>(addr_lo, addr_hi, dptr_basis, dyn->d_un.d_ptr);
              break;
            default:
              break;
            }
          }
          break;
        }
        case PT_NOTE:
          note = reinterpret_cast<const ElfW(Nhdr)*>(info->dlpi_addr + phdr.p_vaddr);
          note_end = reinterpret_cast<const ElfW(Nhdr)*>(info->dlpi_addr + phdr.p_vaddr + phdr.p_memsz);
          while (note < note_end)
          {
            const char* n_name = reinterpret_cast<const char*>(note) + sizeof(*note);
            if (note->n_type == NT_GNU_BUILD_ID &&
                note->n_descsz != 0 &&
                note->n_namesz == 4 &&
                memcmp(n_name, "GNU", 4) == 0)
            {
              const char* n_build_id = n_name + 4;
              size_t size = (note->n_descsz > elf_hash_size) ? elf_hash_size : note->n_descsz;
              has_build_id = true;
              memcpy(&build_id[0],n_build_id,size);
            }
#ifdef UPCXXI_UPCXX_ELF_NOTE_NAME
            if (note->n_type == 0x01234567 &&
                note->n_descsz != 0 &&
                note->n_namesz == sizeof(UPCXXI_UPCXX_ELF_NOTE_NAME) &&
                strcmp(n_name, UPCXXI_UPCXX_ELF_NOTE_NAME) == 0)
            {
              flags |= (int)segment_flags::upcxx_binary;
            }
#endif
            note = reinterpret_cast<const ElfW(Nhdr)*>(reinterpret_cast<const char*>(note) + sizeof(*note) +
                      ALIGN_UP(note->n_namesz, 4) +
                      ALIGN_UP(note->n_descsz, 4));
          }
          break;
        default:
          break;
        }
      }

      if (hashtbl)
      {
        symbol_count = hashtbl[1];
      } else if (gnuhashtbl)
      {
        const uint32_t *buckets = gnuhashtbl + 4 + gnuhashtbl[2] * sizeof(uintptr_t)/sizeof(uint32_t);
        for (uint32_t i = 0; i < gnuhashtbl[0]; ++i) {
          if (buckets[i] > symbol_count)
            symbol_count = buckets[i];
        }
        if (symbol_count)
        {
          //Subtract the symtaboffset, the skippable symbols (STN_UNDEF, etc) at the beginning, not in hash table
          symbol_count -= gnuhashtbl[1];
          //Go to the start of the last chain
          const uint32_t *hashval = buckets + gnuhashtbl[0] + symbol_count;
          //Step through the last chain, end marked by 1 in lowest bit
          do symbol_count++;
          while (!(*hashval++ & 1));
        }
      }

      if (symbol_count)
        symtblend = symtbl + symbol_count;
      else
        symtblend = reinterpret_cast<const ElfW(Sym)*>(strtbl);

      for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD)
        {
          if (phdr.p_flags & PF_R && phdr.p_flags & PF_X)
          {
            segment_info seg{};
            seg.start = info->dlpi_addr + phdr.p_vaddr;
            seg.end = seg.start + phdr.p_memsz;
#if GASNETT_SPEC_VERSION_MAJOR > 1 || GASNETT_SPEC_VERSION_MINOR >= 19
            if (phdr_num > 0)
              seg.dlpi_name = info->dlpi_name;
            else
              seg.dlpi_name = gasnett_exe_name();
#else
            seg.dlpi_name = info->dlpi_name;
#endif
            seg.flags = flags;
            seg.segnum = std::numeric_limits<decltype(seg.segnum)>::max();
            if (phdr.p_flags & PF_W || has_textrel || has_build_id)
            {
              seg.segnum = i;
              if (has_build_id)
                seg.lib_hash.hash = build_id;
            }
            map.emplace_back(std::move(seg));
          }
        }
      }

      /*
       * We could also map data segments using one of the constant identifiers + segment
       * number. Heap data ranges could be manually registered or use an allocator that would
       * perform this registration. This would allow relocating data pointers. However, until
       * C++ has reflection, there are few advantages over global_ptr and dist_objects.
       * With reflection, this could allow transparent relocation of captured references and
       * pointers.
       */

      /*
       * Hash a bit of information that should be the same across all processes.
       */
      for (size_t i = orig_elements; i < map.size(); ++i)
      {
        segment_info& seg = map[i];
        seg.symtbl = symtbl;
        seg.symtblend = symtblend;
        seg.strtbl = strtbl;
        static_assert(sizeof(seg.basis) == sizeof(dptr_basis), "ElfW(Addr) incompatible with uintptr_t");
        seg.basis = static_cast<uintptr_t>(dptr_basis);
        if (has_build_id)
        {
          seg.ident = seg.lib_hash;
          seg.ident.add_segnum(seg.segnum);
#if !UPCXXI_EXEFORMAT_ELF_UNREADABLE
        } else if (seg.lib_hash.is_seghash()) {
          /*
           * Here, the read-only memory is hashed to give an identifier, no matter if the libraries are loaded
           * differently, as long as the libraries are identical. This also ensures the same versions are loaded.
           *
           * OpenBSD may include an unreadable .boot.text segment, which is mapped, but unreadable, causing
           * a segfault. This could be worked around by opening the entire DSO again to be able to parse the
           * section headers and map their offsets, which would also give access to the full symbol/string
           * tabels rather than just the dynamic versions, but this is more effort than reasonable to put into
           * an unsupported OS. Just disable the unportable code.
           */
          fnv128 h{};
          h.block_in(reinterpret_cast<const char*>(seg.start),reinterpret_cast<const char*>(seg.end));
          seg.ident = h;
          //memcpy(&seg.hash[sizeof(seg.hash)-sizeof(hash)],reinterpret_cast<uint8_t*>(&hash),sizeof(hash));
#endif
        } else {
          /*
           * If .text is edited or the entire segment is possibly not readable, fall back to hashing the library
           * path plus section number. This isn't 100% reliable, as things like symlinks and bind mounts may
           * result in disagreements.
           */
          if (seg.dlpi_name && strlen(seg.dlpi_name) > 0)
          {
            fnv128 h{};
            h.string_in(seg.dlpi_name);
            seg.lib_hash = h;
            seg.ident = h;
            seg.ident.add_segnum(seg.segnum);
            //memcpy(&seg.hash[sizeof(seg.hash)-sizeof(hash)],reinterpret_cast<uint8_t*>(&hash),sizeof(hash));
          } else {
            /*
             * Writable or TEXTREL segment
             *
             * Don't hash an empty string, as those would collide. The main executable is an empty string
             * and Summit, at one point, named what appeared to be its vDSO as an empty string.
             *
             * If the user wants to run executables with self-modifying .text, the easiest solution would be
             * to just compile with -Wl,--build-id.
             *
             * Don't actually throw here. There's no guarantee such a segment will be addressed, and therefore
             * might not be a problem. Instead, mark it as a bad segment, allowing the unrelocatable pointer
             * and bad segment address range to be printed together when this fatal situation is encountered.
             */
            seg.flags |= (uint16_t) segment_flags::bad_segment;
          }
        }
      }
      phdr_num++;
      return 0;
    }, static_cast<void*>(&tdata));
    for (size_t i = 0; i < map.size(); ++i)
    {
      for (size_t j = i+1; j < map.size(); ++j)
      {
        if (map[i].ident == map[j].ident) {
          map[i].flags |= (uint16_t) segment_flags::bad_segment;
          map[j].flags |= (uint16_t) segment_flags::bad_segment;
        }
      }
    }
    return map;
  }

#elif UPCXXI_EXEFORMAT_MACHO
  std::vector<segment_info> segmap_cache::build_segment_map()
  {
    std::vector<segment_info> map;
    dyld_all_image_infos* all_infos = macho_all_infos();
    uint32_t infos_count = _dyld_image_count();
    for (size_t i = 0; i < infos_count; ++i)
    {
      const dyld_image_info* info = all_infos->infoArray + i;
      const mach_header_64* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
      if (header->magic != MH_MAGIC_64) continue;

      uint32_t ncmds = header->ncmds;
      const load_command* load_cmd = reinterpret_cast<const load_command*>(header+1);

      const uint8_t* uuid = nullptr;
      bool upcxx_segment = false;

      for (uint32_t j = 0; j < ncmds; ++j)
      {
        switch (load_cmd->cmd)
        {
        case LC_SEGMENT_64:
        {
          const segment_command_64* sc = reinterpret_cast<const segment_command_64*>(load_cmd);
          if (strcmp(sc->segname, SEG_TEXT) == 0)
          {
            const section_64* sect = reinterpret_cast<const section_64*>(sizeof(segment_command_64) + reinterpret_cast<uintptr_t>(sc));
            for (uint32_t k = 0; k < sc->nsects; ++k)
            {
              if (strcmp(sect->sectname, "upcxx") == 0)
                upcxx_segment = true;
              sect = reinterpret_cast<const section_64*>(sizeof(section_64) + reinterpret_cast<uintptr_t>(sect));
            }
          }
          break;
        }
        case LC_UUID:
        {
          const uuid_command* uuidc = reinterpret_cast<const uuid_command*>(load_cmd);
          uuid = uuidc->uuid;
          break;
        }
        default:
          break;
        }
        load_cmd = reinterpret_cast<const load_command*>(load_cmd->cmdsize + reinterpret_cast<uintptr_t>(load_cmd));
      }

      load_cmd = reinterpret_cast<const load_command*>(header+1);

      for (uint32_t j = 0; j < ncmds; ++j)
      {
        switch (load_cmd->cmd)
        {
        case LC_SEGMENT_64:
        {
          const segment_command_64* sc = reinterpret_cast<const segment_command_64*>(load_cmd);
          if (strcmp(sc->segname, SEG_TEXT) == 0)
          {
            intptr_t slide = _dyld_get_image_vmaddr_slide(i);
            uintptr_t lo = sc->vmaddr + slide;
            uintptr_t hi = lo + sc->vmsize;
            uint16_t flags = 0;
            if (upcxx_segment)
              flags |= (uint16_t) segment_flags::upcxx_binary;
            if (uuid) {
              map.emplace_back(segment_info{lo, hi, {uuid,j}, {uuid}, static_cast<uint16_t>(j), flags, info->imageFilePath});
            } else {
              fnv128 h{};
              h.block_in(reinterpret_cast<const char*>(lo),reinterpret_cast<const char*>(hi));
              map.emplace_back(segment_info{lo, hi, {h}, {h}, static_cast<uint16_t>(j), flags, info->imageFilePath});
            }
          }
          break;
        }
        default:
          break;
        }
        load_cmd = reinterpret_cast<const load_command*>(load_cmd->cmdsize + reinterpret_cast<uintptr_t>(load_cmd));
      }
    }
    for (size_t i = 0; i < map.size(); ++i)
    {
      for (size_t j = i+1; j < map.size(); ++j)
      {
        if (map[i].ident == map[j].ident) {
          map[i].flags |= (uint16_t) segment_flags::bad_segment;
          map[j].flags |= (uint16_t) segment_flags::bad_segment;
        }
      }
    }
    return map;
  }

#endif
#else
  std::vector<segment_info> segmap_cache::build_segment_map()
  {
    std::vector<segment_info> map;
    /*
     * In legacy mode, use only function_token_ss. function_token_ss assumes the pointer is within its range: The
     * range check for the primary segment happens in function_token when deciding its internal token type. Adding a
     * dummy segment into the segment map at the address of the fallback_primary_segment_sentinel causes the primary
     * segment lookup mechanisms to use this pointer as its basis. This pointer isn't at the start of the segment, but
     * the pointer arithmetic still works, as the offset is constant.
     *
     * Some of the debug_write functions fail due to other functions being outside this range, but those are of little
     * use with single-upc++-segment applications. These are still available in non-legacy single-segment mode, as the
     * segment processing is still run in that case.
     */
    uintptr_t uptr = fnptr_to_uintptr(&segmap_cache::fallback_primary_segment_sentinel);
    fnv128 h{};
    map.emplace_back(segment_info{uptr,uptr+1, {h}, {h}, 0, 0, "Legacy tokenization"});
    return map;
  }
#endif

  void segmap_cache::rebuild_segment_map()
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    auto newmap = build_segment_map();
    constexpr uint16_t keepflags = (uint16_t) segment_flags::active |
                                   (uint16_t) segment_flags::verified |
                                   (uint16_t) segment_flags::bad_verification;
    for (auto& nseg : newmap)
    {
      for (const auto& oseg : segmap)
      {
        if (nseg.ident == oseg.ident)
          nseg.flags |= oseg.flags & keepflags;
      }
    }
    flag_map_.clear();
    for (const auto& seg : newmap)
      flag_map_[seg.start] = seg.flags;
    segmap = newmap;
  }

  inline bool is_stream_tty(std::ostream& out)
  {
    if (out.rdbuf() == std::cout.rdbuf())
      return upcxx::experimental::os_env<bool>("UPCXX_COLORIZE_DEBUG", isatty(1));
    else if (out.rdbuf() == std::cerr.rdbuf())
      return upcxx::experimental::os_env<bool>("UPCXX_COLORIZE_DEBUG", isatty(2));
    else
      return upcxx::experimental::os_env<bool>("UPCXX_COLORIZE_DEBUG", false);
  }

  size_t segmap_cache::find_max_namelen()
  {
    size_t min_namelen = 50;
    size_t max_namelen = min_namelen;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    for (const auto& seg : segmap)
    {
      size_t len = strlen(seg.dlpi_name);
      if (len > max_namelen)
        max_namelen = len;
    }
    return max_namelen;
  }

  inline void debug_symbol_header(uintptr_t uptr, std::ostream& ss, size_t table_width)
  {
    const char* dli_sname = segmap_cache::get_symbol(uptr);
    if (dli_sname)
    {
      int status = 0;
#if UPCXXI_HAVE___CXA_DEMANGLE
      const char* dname = abi::__cxa_demangle(dli_sname, 0, 0, &status);
      if (status != 0)
        dname = dli_sname;
#else
      const char* dname = dli_sname;
#endif
      ss << "[" << rank_me() << "] | Symbol: " << std::setfill(' ') << std::setw(table_width - 11) << std::left << dname << "|\n";
#if UPCXXI_HAVE___CXA_DEMANGLE
      if (status == 0)
        free((void*)dname);
#endif
    }
  }

  inline void debug_ptr_header(uintptr_t uptr, std::ostream& ss, size_t table_width, int color)
  {
    bool bcolor = !!color;
    if (color == 2)
      bcolor = is_stream_tty(ss);
    if (uptr)
    {
      const char* color_start = "";
      const char* style_start = "";
      const char* color_end = "";
      if (bcolor) {
        color_start = segmap_cache::success_start;
        style_start = segmap_cache::bold;
        color_end = segmap_cache::ccolor_end;
      }
      ss << "[" << rank_me() << "] | Pointer: " << color_start << style_start << std::setfill(' ') << std::setw(table_width-26) << std::left << reinterpret_cast<void*>(uptr) << color_end << "|\n" << std::right;
    }
  }

  inline void debug_table_write_hline(std::ostream& ss, size_t cwidth_name)
  {
    ss << "[" << rank_me() << "] |";
    ss << std::setw(cwidth_name+1) << std::right << std::setfill('-') << '|';
    ss << std::setw(segmap_cache::cwidth_hash+1) << '|';
    ss << std::setw(segmap_cache::cwidth_segment+1) << '|';
    ss << std::setw(segmap_cache::cwidth_flags+1) << '|';
    ss << std::setw(segmap_cache::cwidth_pointer+1) << '|';
    ss << std::setw(segmap_cache::cwidth_pointer+1) << '|';
    ss << '\n' << std::setfill(' ');
  }

  void segmap_cache::debug_write_table(int fd, int color)
  {
    std::stringstream ss;
    debug_write_table(ss, should_debug_color(fd,color));
    write_helper(fd, ss.str().c_str(), ss.str().size());
  }

  void segmap_cache::debug_write_table(std::ostream& ss, int color, size_t max_namelen, bool print_top, size_t found_index, bool buffer)
  {
    bool bcolor = !!color;
    if (color == 2)
      bcolor = is_stream_tty(ss);
    const char* color_start = "";
    const char* color_end = "";
    const char* style_start = "";
    if (max_namelen == 0)
      max_namelen = find_max_namelen();
    size_t cwidth_name = max_namelen + padding + cwidth_indicator;
    size_t table_width = cwidth_name+cwidth_hash+cwidth_segment+cwidth_flags+cwidth_pointer*2+cols+1;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    if (print_top)
      ss << "[" << rank_me() << "] " << std::setfill('-') << std::right << std::setw(table_width+1) << '\n';
    ss << "[" << rank_me() << "] |" << std::setfill(' ') << std::left << std::setw(cwidth_name) << (std::string(" dlpi_name (rank_me: ") + std::to_string(rank_me()) + ")")
       << '|' << std::setw(cwidth_hash) << " hash "
       << '|' << std::setw(cwidth_segment) << " segment # "
       << '|' << std::setw(cwidth_flags) << " flags "
       << '|' << std::setw(cwidth_pointer) << " start_addr "
       << '|' << std::setw(cwidth_pointer) << " end_addr " << "|\n";
    debug_table_write_hline(ss,cwidth_name);
    for (size_t i = 0; i < segmap.size(); ++i)
    {
      const auto& seg = segmap[i];
      if (bcolor) {
        color_start = "";
        style_start = "";
        if ((segmap[i].flags & (uint16_t) segment_flags::bad_verification)
            || (enforce_verification_ && !(seg.flags & (uint16_t) segment_flags::verified)))
          color_start = "\033[93m";
        else if (segmap[i].flags & (uint16_t) segment_flags::bad_segment)
          color_start = "\033[91m";
        else if (i == found_index)
          color_start = success_start;
        else if (segmap[i].flags & (uint16_t) segment_flags::verified)
          color_start = "\033[96m";
        else if (segmap[i].flags & (uint16_t) segment_flags::active)
          style_start = "\033[1m";
        else if (segmap[i].flags & (uint16_t) segment_flags::upcxx_binary)
          color_start = "\033[94m";
        color_end = ccolor_end;
      }
      char mark = ' ';
      if (i == found_index)
        mark = '*';
      size_t len = max_namelen - strlen(seg.dlpi_name) + 1;
      ss << "[" << rank_me() << "] | " << style_start << color_start << mark << ' ' << seg.dlpi_name << std::setw(len) << ' ' << color_end << "| " << style_start << color_start;
      ss << std::setfill('0') << std::hex;
      for (size_t j = 0; j < segment_hash::size; ++j)
        ss << std::setw(2) << static_cast<int>(seg.ident.hash[j]);
      ss << std::setfill(' ') << std::dec << color_end << " | ";
      if (seg.segnum == std::numeric_limits<decltype(seg.segnum)>::max())
        ss << style_start << color_start << "segment hash" << color_end;
      else
        ss << style_start << color_start << std::setw(cwidth_segment-padding) << seg.segnum << color_end;
      ss << " | " << style_start << color_start << std::setw(cwidth_flags-padding) << seg.flags << color_end;
      ss << std::setfill('0') << std::internal; 
      ss << " | " << style_start << color_start << std::setw(cwidth_pointer-padding) << reinterpret_cast<void*>(seg.start) << color_end;
      ss << " | " << style_start << color_start << std::setw(cwidth_pointer-padding) << reinterpret_cast<void*>(seg.end) << color_end;
      ss << std::setfill(' ');
      ss << " |\n";
    }
    ss << "[" << rank_me() << "] " << std::setfill('-') << std::setw(table_width) << '-' << '\n' << std::setfill(' ');
  }

  void segmap_cache::debug_write_ptr(uintptr_t uptr, int fd, int color)
  {
    std::stringstream ss;
    debug_write_ptr(uptr, ss, should_debug_color(fd,color));
    write_helper(fd, ss.str().c_str(), ss.str().size());
  }

  void segmap_cache::debug_write_ptr(uintptr_t uptr, std::ostream& ss, int color)
  {
    bool bcolor = !!color;
    if (color == 2)
      bcolor = is_stream_tty(ss);
    const char* color_start = "";
    const char* style_start = "";
    const char* color_end = "";
    const char* lookup_res = lookup_failure;
    size_t found_index = (size_t)-1;
    bool bad_segment = false;
    bool bad_verification = false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();

    for (size_t i = 0; i < segmap.size(); ++i)
    {
      const auto& seg = segmap[i];
      if (uptr >= seg.start && uptr < seg.end)
      {
        found_index = i;
        if (seg.flags & (uint16_t) segment_flags::bad_segment) {
          bad_segment = true;
        } else if ((seg.flags & (uint16_t) segment_flags::bad_verification) ||
            (enforce_verification_ && !(seg.flags & (uint16_t) segment_flags::verified))) {
          lookup_res = "BAD VERIFICATION";
          bad_verification = true;
        } else {
          lookup_res = lookup_success;
        }
        break;
      }
    }

    if (bcolor) {
      color_end = ccolor_end;
      style_start = bold;
      if (found_index != (size_t)-1 && !bad_segment && !bad_verification)
        color_start = success_start;
      else
        color_start = failure_start;
    }

    size_t max_namelen = find_max_namelen();
    size_t cwidth_name = max_namelen + padding + cwidth_indicator;
    size_t table_width = cwidth_name+cwidth_hash+cwidth_segment+cwidth_flags+cwidth_pointer*2+cols+1;

    ss << "[" << rank_me() << "] " << std::setw(table_width+1) << std::setfill('-') << '\n';
    const char pointer_desc[] = "Lookup for pointer: ";
    ss << std::setfill(' ');
    ss << "[" << rank_me() << "] | " << pointer_desc << color_start << style_start << std::setw(cwidth_pointer-padding+2) << std::hex << reinterpret_cast<void*>(uptr) << std::dec << " (" << lookup_res << ")" << color_end;
    size_t sz = table_width - sizeof(pointer_desc) - cwidth_pointer - 4 /*" () "*/ - strlen(lookup_res) + 1;
    ss << std::setw(sz) << std::setfill(' ') << std::right << "|\n";
    debug_symbol_header(uptr, ss, table_width);
    ss << "[" << rank_me() << "] |" << std::setw(table_width) << std::setfill('-') << std::right << "|\n";
    debug_write_table(ss, bcolor, max_namelen, false, found_index);
  }

  void segmap_cache::debug_write_token(const function_token_ms& token, int fd, int color)
  {
    std::stringstream ss;
    debug_write_token(token, ss, should_debug_color(fd,color));
    write_helper(fd, ss.str().c_str(), ss.str().size());
  }

  void segmap_cache::debug_write_token(const function_token_ms& token, std::ostream& ss, int color)
  {
    bool bcolor = !!color;
    if (color == 2)
      bcolor = is_stream_tty(ss);
    const char* color_start = "";
    const char* style_start = "";
    const char* color_end = "";
    const char* lookup_res = lookup_failure;
    size_t found_index = (size_t)-1;
    uintptr_t uptr = 0;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();

    for (size_t i = 0; i < segmap.size(); ++i)
    {
      const auto& seg = segmap[i];
      if (seg.ident == token.ident && token.offset < seg.end-seg.start)
      {
        found_index = i;
        lookup_res = lookup_success;
        uptr = seg.start + token.offset;
        break;
      }
    }

    if (bcolor) {
      color_end = ccolor_end;
      style_start = bold;
      if (found_index != (size_t)-1)
        color_start = success_start;
      else
        color_start = failure_start;
    }

    size_t max_namelen = find_max_namelen();
    size_t cwidth_name = max_namelen + padding + cwidth_indicator;
    size_t table_width = cwidth_name+cwidth_hash+cwidth_segment+cwidth_flags+cwidth_pointer*2+cols+1;
    ss << "[" << rank_me() << "] " << std::setw(table_width+1) << std::setfill('-') << '\n';
    const char token_desc[] = "Lookup for token: ";
    ss << "[" << rank_me() << "] | " << token_desc << color_start << style_start;
    std::stringstream ss2;
    ss2 << '{' << std::setfill('0') << std::hex;
    for (size_t j = 0; j < segment_hash::size; ++j)
      ss2 << std::setw(2) << static_cast<int>(token.ident.hash[j]);
    ss2 << ", " << token.offset << "} (" << lookup_res << ')' << std::dec;
    ss << std::dec << std::setfill(' ');
    ss << std::left << std::setw(table_width-2-sizeof(token_desc)) << ss2.str() << std::right << color_end << "|\n";
    debug_symbol_header(uptr, ss, table_width);
    debug_ptr_header(uptr, ss, table_width, bcolor);
    ss << "[" << rank_me() << "] |" << std::setfill('-') << std::setw(table_width) << "|\n";
    debug_write_table(ss, bcolor, max_namelen, false, found_index);
  }

  typename std::vector<segment_info>::iterator segmap_cache::try_inactive(uintptr_t uptr)
  {
    using std::begin;
    using std::end;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    auto it = begin(segmap);
    for (; it != end(segmap); ++it)
    {
      if (uptr > it->start && uptr < it->end)
      {
        if (it->flags & (uint16_t) segment_flags::bad_segment)
        {
          std::stringstream ss;
          ss << "Attempted to activate a duplicate, RWX, or TEXTREL segment from library with unknown file path. See: docs/ccs-rpc.md.\n\n";
          debug_write_ptr(uptr, ss);
          UPCXXI_FATAL_ERROR(ss.str());
        }
        // Found in inactive cache. Activate.
        activate(*it);
        break;
      }
    }
    return it;
  }

  void segmap_cache::activate(segment_info& seg)
  {
    using std::begin;
    using std::end;
    seg.flags |= (uint16_t) segment_flags::active;
    size_t l1_size = max_level1_cache_size;
    if (cache_occupancy_ < max_level1_cache_size)
    {
      l1_cache_ptr_[cache_occupancy_] = segment_lookup_ptr{seg.start, seg.end, seg.ident};
      l1_cache_tkn_[cache_occupancy_] = segment_lookup_tkn{seg.ident, seg.start};
      ++cache_occupancy_;
      l1_size = cache_occupancy_;
      std::sort(begin(l1_cache_ptr_), begin(l1_cache_ptr_)+l1_size, [](const segment_lookup_ptr& lhs, const segment_lookup_ptr& rhs)
      {
        return lhs.start < rhs.start;
      });
      std::sort(begin(l1_cache_tkn_), begin(l1_cache_tkn_)+l1_size, [](const segment_lookup_tkn& lhs, const segment_lookup_tkn& rhs)
      {
        return lhs.ident < rhs.ident;
      });
    } else {
#if UPCXXI_ASSERT_ENABLED
      static bool emitted_warning = false;
      if (!emitted_warning)
      {
        experimental::say() << "WARNING: Level 1 dynamic linking relocation cache exceeded capacity.\n";
        emitted_warning = true;
      }
#endif
      /*
       * L2 cache is rebuilt with rebuild_cache(), as L2 cache is process-wide
       */
    }
  }

  segment_info segmap_cache::find_primary_upcxx_segment()
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    for (const auto& seg : segmap)
    {
      if (seg.flags & (uint16_t) segment_flags::upcxx_binary)
        return seg;
    }
    // No upcxx_binary flag found. Fall back to the segment
    // where segmap_cache::fallback_primary_segment_identifier is
    // located.
    uintptr_t uptr = fnptr_to_uintptr(&segmap_cache::fallback_primary_segment_sentinel);
    for (const auto& seg : segmap)
    {
      if (uptr >= seg.start && uptr < seg.end)
        return seg;
    }
    // This should be unreachable. If the class's own function
    // can't be mapped, something is seriously broken.
    UPCXXI_FATAL_ERROR("Unable to find primary UPC++ segment.");
    UPCXXI_UNREACHABLE();
  }

  static segment_hash seghash_reduce(const segment_hash& lhs, const segment_hash& rhs)
  {
    if (lhs == rhs)
      return lhs;
    else
      return {};
  };

  void segmap_cache::set_primary_segment(uintptr_t uptr, upcxx::entry_barrier eb)
  {
    backend::quiesce(world(), eb);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    for (const auto& seg : segmap)
    {
      if (uptr >= seg.start && uptr < seg.end)
      {
        primary_ = seg;
        segment_hash reduced = reduce_all(seg.ident, seghash_reduce).wait();
        if (reduced == segment_hash{})
          UPCXXI_FATAL_ERROR("Inconsistent segment hash while attempting to set primary segment.");
        return;
      }
    }
    UPCXXI_FATAL_ERROR("Unable to set primary UPC++ segment: Supplied pointer not in address range of an executable segment.");
  }

  void segmap_cache::rebuild_cache()
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    rebuild_segment_map();
    l2_cache_ptr_ = build_cache_ptr();
    l2_cache_tkn_ = build_cache_tkn();
  }

  decltype(segmap_cache::l2_cache_ptr_) segmap_cache::build_cache_ptr()
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    decltype(segmap_cache::l2_cache_ptr_) ret{};

    for (const auto& seg : segmap)
      ret.emplace_back(segment_lookup_ptr{seg.start, seg.end, seg.ident});

    using std::begin;
    using std::end;
    std::sort(begin(ret), end(ret), [](const segment_lookup_ptr& lhs, const segment_lookup_ptr& rhs)
    {
      return lhs.start < rhs.start;
    });
    return ret;
  }

  decltype(segmap_cache::l2_cache_tkn_) segmap_cache::build_cache_tkn()
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& segmap = segment_map();
    decltype(segmap_cache::l2_cache_tkn_) ret;

    for (const auto& seg : segmap)
      ret[seg.ident] = seg.start;

    return ret;
  }

  void segmap_cache::verify_segment(uintptr_t uptr, entry_barrier eb)
  {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_ALWAYS_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);
    backend::quiesce(world(), eb);
#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
    using std::begin;
    using std::end;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    rebuild_segment_map();
    auto& segmap = segment_map();
    auto it = begin(segmap);
    for (; it != end(segmap); ++it)
    {
      if (uptr > it->start && uptr < it->end)
      {
        if (it->flags & (uint16_t) segment_flags::bad_segment)
        {
          std::stringstream ss;
          ss << "Attempted to use a duplicate, RWX, or TEXTREL segment from library with unknown file path. See: docs/ccs-rpc.md.\n\n";
          debug_write_ptr(uptr, ss);
          UPCXXI_FATAL_ERROR(ss.str());
        }
        break;
      }
    }
    auto binop = [](const segment_hash& lhs, const segment_hash& rhs) -> segment_hash {
      if (lhs == rhs)
        return lhs;
      else
        return {};
    };
    segment_hash h{};
    if (it != end(segmap))
      h = it->ident;
    segment_hash reduced = reduce_all(h, binop).wait();
    if (it != end(segmap)) {
      if (reduced != segment_hash{}) {
        it->flags |= (uint16_t) segment_flags::verified;
      } else {
        it->flags |= (uint16_t) segment_flags::bad_verification;
        throw segment_verification_error("verify_segment() failed: Segment not found on all ranks.");
      }
      flag_map_[it->start] = it->flags;
    } else {
      throw segment_verification_error("verify_segment() failed: Segment not found on all ranks.");
    }
#endif
  }

  void segmap_cache::verify_all(entry_barrier eb)
  {
    UPCXXI_ASSERT_INIT();
    UPCXXI_ASSERT_ALWAYS_MASTER();
    UPCXXI_ASSERT_MASTER_CURRENT_IFSEQ();
    UPCXXI_ASSERT_COLLECTIVE_SAFE(eb);
    backend::quiesce(world(), eb);
#if !UPCXXI_FORCE_LEGACY_RELOCATIONS
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    rebuild_segment_map();
    auto& segmap = segment_map();
    size_t segment_count = broadcast(segmap.size(), 0).wait();
    std::unique_ptr<segment_hash[]> hashlist(new segment_hash[segment_count]);
    if (rank_me() == 0)
      for (std::size_t i = 0; i < segment_count; ++i)
        new (&hashlist[i]) segment_hash(segmap[i].ident);
    broadcast(hashlist.get(), segment_count, 0).wait();

    std::unique_ptr<bool[]> checklist(new bool[segment_count]());

    for (std::size_t i = 0; i < segment_count; ++i) {
      for (const auto& seg : segmap) {
        if (hashlist[i] == seg.ident) {
          checklist[i] = true;
          break;
        }
      }
    }

    reduce_all(checklist.get(), checklist.get(), segment_count, op_fast_mul).wait();

    for (std::size_t i = 0; i < segment_count; ++i) {
      for (auto& seg : segmap) {
        if (seg.ident == hashlist[i])
        {
          if (checklist[i])
            seg.flags |= static_cast<uint16_t>(segment_flags::verified);
          else
            seg.flags |= static_cast<uint16_t>(segment_flags::bad_verification);
          break;
        }
      }
    }

    uint16_t found_flags = static_cast<uint16_t>(segment_flags::verified) | static_cast<uint16_t>(segment_flags::bad_verification);
    flag_map_.clear();
    for (auto& seg : segmap) {
      if (!(seg.flags & found_flags))
        seg.flags |= static_cast<uint16_t>(segment_flags::bad_verification);
      flag_map_[seg.start] = seg.flags;
    }
#endif
  }

  std::string verification_failed_message(uintptr_t start, uintptr_t end, uintptr_t uptr)
  {
    char buffer[400]{};
    snprintf(buffer, sizeof(buffer), "Attempted to use unverified segment [%" PRIxPTR "-%" PRIxPTR "] to relocate function pointer %" PRIxPTR ".\n\n", start, end, uptr);
    std::stringstream ss;
    ss << buffer;
    segmap_cache::debug_write_ptr(uptr, ss);
    return ss.str();
  }

  std::string tokenization_failed_message(uintptr_t uptr)
  {
    std::stringstream ss;
    ss << "Attempted tokenization of function pointer not found in any executable segment. See: docs/ccs-rpc.md.\n\n";
    segmap_cache::debug_write_ptr(uptr, ss, segmap_cache::should_debug_color(2,2));
    return ss.str();
  }

  std::string detokenization_failed_message(const function_token_ms& token)
  {
    std::stringstream ss;
    ss << "Attempted detokenization in unknown executable segment. See: docs/ccs-rpc.md.\n\n";
    segmap_cache::debug_write_token(token, ss, segmap_cache::should_debug_color(2,2));
    return ss.str();
  }

  bool segmap_cache::should_debug_color(int fd, int color)
  {
    UPCXX_ASSERT(color >= 0 && color <= 2, "Color choice out of range");
    if (color < 2) return !!color;
    else return upcxx::experimental::os_env<bool>("UPCXX_COLORIZE_DEBUG", isatty(fd));
  }

  void segmap_cache::fallback_primary_segment_sentinel() {}

  std::unordered_map<uintptr_t,uint16_t> segmap_cache::flag_map_ = []() {
    auto& segmap = segmap_cache::segment_map();
    std::unordered_map<uintptr_t,uint16_t> ret;
    for (const auto& seg : segmap)
      ret[seg.start] = seg.flags;
    return ret;
  }();

  std::recursive_mutex segmap_cache::mutex_{};
  segment_info segmap_cache::primary_ = find_primary_upcxx_segment();
  std::vector<segmap_cache::segment_lookup_ptr> segmap_cache::l2_cache_ptr_ = segmap_cache::build_cache_ptr();
  std::unordered_map<segment_hash,uintptr_t> segmap_cache::l2_cache_tkn_ = segmap_cache::build_cache_tkn();

  bool segmap_cache::enforce_verification_{!!UPCXXI_ASSERT_ENABLED};
  constexpr const char segmap_cache::success_start[];
  constexpr const char segmap_cache::failure_start[];
  constexpr const char segmap_cache::bold[];
  constexpr const char segmap_cache::ccolor_end[];
  constexpr const char segmap_cache::lookup_success[];
  constexpr const char segmap_cache::lookup_failure[];
  constexpr size_t segmap_cache::padding;
  constexpr size_t segmap_cache::cwidth_indicator;
  constexpr size_t segmap_cache::cwidth_hash;
  constexpr size_t segmap_cache::cwidth_segment;
  constexpr size_t segmap_cache::cwidth_flags;
  constexpr size_t segmap_cache::cwidth_pointer;
  constexpr size_t segmap_cache::cols;

  void function_token_ss::debug_write(int fd, const segmap_cache& cache, int color) const
  {
    uintptr_t uptr = cache.primary().start + offset;
    const char* dli_sname = segmap_cache::get_symbol(uptr);
#if UPCXXI_HAVE___CXA_DEMANGLE
    const char* dname = nullptr;
    if (dli_sname) {
      dname = abi::__cxa_demangle(dli_sname, 0, 0, 0);
    }
#else
    const char* dname = dli_sname;
#endif
    int size = snprintf(nullptr, 0, "function_token_ss [%" PRIxPTR " - %" PRIxPTR "]: {offset: %" PRIxPTR ", basis: %" PRIxPTR ", pointer: %p, symbol: %s}\n", cache.primary().start, cache.primary().end, offset, cache.primary().start, reinterpret_cast<void*>(uptr), dname);
    char *buffer = new char[size+1];
    sprintf(buffer, "function_token_ss [%" PRIxPTR " - %" PRIxPTR "]: {offset: %" PRIxPTR ", basis: %" PRIxPTR ", pointer: %p, symbol: %s}\n", cache.primary().start, cache.primary().end, offset, cache.primary().start, reinterpret_cast<void*>(uptr), dname);
    write_helper(fd, buffer, size);
    delete[] buffer;
#if UPCXXI_HAVE___CXA_DEMANGLE
    free((void*)dname);
#endif
  }

  void function_token_ms::debug_write(int fd, segmap_cache& cache, int color) const
  {
    cache.debug_write_token(*this,fd,color);
  }

  void function_token::debug_write(int fd, segmap_cache& cache, int color) const
  {
    if (active == function_token::identifier::single)
      s.debug_write(fd,cache,color);
    else
      m.debug_write(fd,cache,color);
  }

}} // namespace upcxx::detail

