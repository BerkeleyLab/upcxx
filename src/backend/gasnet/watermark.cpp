// tools-lite mode, to minimize resources for recompiling this TU
#if GASNET_PAR
#define GASNETT_USE_TRUE_MUTEXES 1 // silence a harmless warning from LTO
#endif
#undef GASNET_SEQ
#undef GASNET_PAR
#define GASNETT_LITE_MODE 1
#include <gasnet_tools.h>

#include <upcxx/version.hpp>
#include <upcxx/upcxx_config.hpp>

////////////////////////////////////////////////////////////////////////
// Library version watermarking
//
// This watermarking file is rebuilt on every rebuild of libupcxx.a
// so minimize the contents of this file to reduce compilation time.
//

#ifndef UPCXX_VERSION
#error  UPCXX_VERSION missing!
#endif
GASNETT_IDENT(UPCXXI_IdentString_LibraryVersion, "$UPCXXLibraryVersion: " _STRINGIFY(UPCXX_VERSION) " $");
#ifndef UPCXX_SPEC_VERSION
#error  UPCXX_SPEC_VERSION missing!
#endif
GASNETT_IDENT(UPCXXI_IdentString_SpecVersion, "$UPCXXSpecVersion: " _STRINGIFY(UPCXX_SPEC_VERSION) " $");

// UPCXXI_GIT_VERSION is defined by the Makefile for this object on the command line,
// when built from a git checkout (with a .git directory) and the git describe command succeeds.
// Otherwise, the git_version.h header is included, which is empty in releases but in snapshots is
// overwritten with a file containing a #define of UPCXXI_GIT_VERSION by the snapshot-building script.
// If none of these sources provide a UPCXXI_GIT_VERSION (eg in a release) this ident string is omitted.
#ifndef UPCXXI_GIT_VERSION
#include <upcxx/git_version.h>
#endif
#ifdef  UPCXXI_GIT_VERSION
GASNETT_IDENT(UPCXXI_IdentString_GitVersion, "$UPCXXGitVersion: " _STRINGIFY(UPCXXI_GIT_VERSION) " $");
#endif

#if UPCXXI_BACKEND_GASNET_SEQ
GASNETT_IDENT(UPCXXI_IdentString_ThreadMode, "$UPCXXThreadMode: SEQ $");
#elif UPCXXI_BACKEND_GASNET_PAR
GASNETT_IDENT(UPCXXI_IdentString_ThreadMode, "$UPCXXThreadMode: PAR $");
#endif

#if GASNET_DEBUG
GASNETT_IDENT(UPCXXI_IdentString_CodeMode, "$UPCXXCodeMode: debug $");
#else
GASNETT_IDENT(UPCXXI_IdentString_CodeMode, "$UPCXXCodeMode: opt $");
#endif

GASNETT_IDENT(UPCXXI_IdentString_GASNetVersion, "$UPCXXGASNetVersion: " 
              _STRINGIFY(GASNET_RELEASE_VERSION_MAJOR) "."
              _STRINGIFY(GASNET_RELEASE_VERSION_MINOR) "."
              _STRINGIFY(GASNET_RELEASE_VERSION_PATCH) " $");

#if UPCXXI_CUDA_ENABLED
  #include <upcxx/cuda.hpp>
  GASNETT_IDENT(UPCXXI_IdentString_KindCUDA, "$UPCXXKindCUDA: " _STRINGIFY(UPCXX_KIND_CUDA) " $");
  GASNETT_IDENT(UPCXXI_IdentString_CUDAEnabled, "$UPCXXCUDAEnabled: 1 $");
#else
  GASNETT_IDENT(UPCXXI_IdentString_CUDAEnabled, "$UPCXXCUDAEnabled: 0 $");
#endif
#if UPCXXI_GEX_MK_CUDA
  GASNETT_IDENT(UPCXXI_IdentString_CUDAGASNet, "$UPCXXCUDAGASNet: 1 $");
#else
  GASNETT_IDENT(UPCXXI_IdentString_CUDAGASNet, "$UPCXXCUDAGASNet: 0 $");
#endif

#if UPCXXI_HIP_ENABLED
  #include <upcxx/hip.hpp>
  GASNETT_IDENT(UPCXXI_IdentString_KindHIP, "$UPCXXKindHIP: " _STRINGIFY(UPCXX_KIND_HIP) " $");
  GASNETT_IDENT(UPCXXI_IdentString_HIPEnabled, "$UPCXXHIPEnabled: 1 $");
#else
  GASNETT_IDENT(UPCXXI_IdentString_HIPEnabled, "$UPCXXHIPEnabled: 0 $");
#endif
#if UPCXXI_GEX_MK_HIP
  GASNETT_IDENT(UPCXXI_IdentString_HIPGASNet, "$UPCXXHIPGASNet: 1 $");
#else
  GASNETT_IDENT(UPCXXI_IdentString_HIPGASNet, "$UPCXXHIPGASNet: 0 $");
#endif

#if UPCXXI_ZE_ENABLED
  #include <upcxx/ze.hpp>
  GASNETT_IDENT(UPCXXI_IdentString_KindZE, "$UPCXXKindZE: " _STRINGIFY(UPCXX_KIND_ZE) " $");
  GASNETT_IDENT(UPCXXI_IdentString_ZEEnabled, "$UPCXXZEEnabled: 1 $");
#else
  GASNETT_IDENT(UPCXXI_IdentString_ZEEnabled, "$UPCXXZEEnabled: 0 $");
#endif
#if UPCXXI_GEX_MK_ZE
  GASNETT_IDENT(UPCXXI_IdentString_ZEGASNet, "$UPCXXZEGASNet: 1 $");
#else
  GASNETT_IDENT(UPCXXI_IdentString_ZEGASNet, "$UPCXXZEGASNet: 0 $");
#endif

#if UPCXXI_FORCE_LEGACY_RELOCATIONS
  GASNETT_IDENT(UPCXXI_IdentString_CCSEnabled, "$UPCXXCCSEnabled: 0 $");
#else
  GASNETT_IDENT(UPCXXI_IdentString_CCSEnabled, "$UPCXXCCSEnabled: 1 $");
#endif

GASNETT_IDENT(UPCXXI_IdentString_AssertEnabled, "$UPCXXAssertEnabled: " _STRINGIFY(UPCXXI_ASSERT_ENABLED) " $");

#if UPCXXI_MPSC_QUEUE_ATOMIC
  GASNETT_IDENT(UPCXXI_IdentString_MPSCQueue, "$UPCXXMPSCQueue: atomic $");
#elif UPCXXI_MPSC_QUEUE_BIGLOCK
  GASNETT_IDENT(UPCXXI_IdentString_MPSCQueue, "$UPCXXMPSCQueue: biglock $");
#endif

GASNETT_IDENT(UPCXXI_IdentString_CompilerID, "$UPCXXCompilerID: " PLATFORM_COMPILER_IDSTR " $");

GASNETT_IDENT(UPCXXI_IdentString_CompilerStd, "$UPCXXCompilerStd: " _STRINGIFY(__cplusplus) " $");

GASNETT_IDENT(UPCXXI_IdentString_BuildTimestamp, "$UPCXXBuildTimestamp: " __DATE__ " " __TIME__ " $");

#ifndef UPCXXI_CONFIGURE_ARGS
#error  UPCXXI_CONFIGURE_ARGS missing!
#endif
GASNETT_IDENT(UPCXXI_IdentString_ConfigureArgs, "$UPCXXConfigureArgs: " UPCXXI_CONFIGURE_ARGS " $");

namespace upcxx { namespace backend { namespace gasnet {
extern int watermark_init();
}}}
// this function exists to ensure this object gets linked into the executable
int upcxx::backend::gasnet::watermark_init() {
  static volatile int dummy = 0;
  int tmp = dummy;
  tmp++;
  dummy = tmp;
  return tmp;
}
