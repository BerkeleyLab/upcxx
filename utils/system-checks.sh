#!/bin/bash

sys_info() {
    # Output information to assist in bug reports
    if test -z "$UPCXX_INSTALL_QUIET" ; then (
        if test -d .git ; then
            echo UPCXX revision: `git describe --always 2> /dev/null`
        fi
        echo System: `uname -a 2>&1`
        /usr/bin/sw_vers 2> /dev/null
        /usr/bin/xcodebuild -version 2> /dev/null 
        /usr/bin/lsb_release -a 2> /dev/null
        echo " "
        echo Date: `date 2>&1`
        echo Current directory: `pwd 2>&1`
        echo Install directory: $PREFIX
        echo "Configure command$fullcmd" # fullcmd starts w/ a colon
        local SETTINGS=
        for var in ${UPCXX_CONFIG_ENVVARS[*]/#/ORIG_} ; do
            if test "${!var:+set}" = set; then
                SETTINGS+="    ${var#ORIG_}='${!var}'\n"
            fi
        done
        echo -n -e "Configure environment:\n$SETTINGS"
        echo " "
        $BASH --version 2>&1 | head -2
        echo " "
    ) fi
}

# Run C or C++ preprocessor to extract an expression matching a bash regex
#
# Arguments:
#   compiler: literal 'CC' or 'CXX'
#       expr: string to be expanded between a pair of delimiters
#      regex: bash regular expression used to extract the actual result
#      flags: (optional) extra compiler flags (such as -I and -D)
#   includes: (optional) string to be expanded first in the source file
#
# The return value is normally the result of the bash regex operation:
#   0 == success (results of match in ${UPCXX_REMATCH[@]})
#   1 == failure
#   2 == bash rejected the regex as invalid
#   3 == other error detected
#
# Notes:
#  + The `expr` should be a valid C expression in a context like
#       identifier1 + expr + identifier2
#    Anything else risks triggering undefined behavior in the preprocessor.
#  + Leading and trailing whitespace in the expansion of `expr` will be lost.
#  + Whitespace internal to the expansion of `expr` might change.
#  + The `regex` should not be so greedy that it matches the delimiters.
#    For instance `.*` would be unsafe, but `(.*)` would be safe (assuming
#    that the parentheses are included in the `expr`).
#  + _CONCAT{,3,4,5,6}() and _STRINGIFY are available
#
cpp_extract_expr() {
    case $1 in
         CC) local suffix=c   cmd="$CC  $CFLAGS   $4 -E";;
        CXX) local suffix=cpp cmd="$CXX $CXXFLAGS $4 -E";;
          *) echo Internal error; exit 3;;
    esac
    local expr="$2"
    local regex="$3"
    local headers="$5"

    local conftest="conftest.$suffix"
    trap "rm -f $conftest" RETURN

    local token1='_6VTuuw1pJg0eJR4d_'
    local token2='_5tf48oGssuj89WUi_'
    rm -f $conftest
    cat >$conftest <<_EOF
      $headers
      #ifndef _CONCAT
        #define _CONCAT_HELPER(a,b) a ## b
        #define _CONCAT(a,b) _CONCAT_HELPER(a,b)
      #endif
      #ifndef _CONCAT3
        #define _CONCAT3(a,b,c) _CONCAT(a,_CONCAT(b,c))
      #endif
      #ifndef _CONCAT4
        #define _CONCAT4(a,b,c,d) _CONCAT(a,_CONCAT3(b,c,d))
      #endif
      #ifndef _CONCAT5
        #define _CONCAT5(a,b,c,d,e) _CONCAT(a,_CONCAT4(b,c,d,e))
      #endif
      #ifndef _CONCAT6
        #define _CONCAT6(a,b,c,d,e,f) _CONCAT(a,_CONCAT5(b,c,d,e,f))
      #endif
      #ifndef _STRINGIFY
        #define _STRINGIFY_HELPER(x) #x
        #define _STRINGIFY(x) _STRINGIFY_HELPER(x)
      #endif
      ${token1}+${expr}+${token2}
_EOF

    # Run preprocessor, capturing output and checking exit code
    local output; # merging `local` and assignment would lose exit code
    local DETAIL_LOG=config-detail.log
    rm -f $DETAIL_LOG
    if ! output=$(eval $cmd $conftest 2> $DETAIL_LOG); then
      echo "ERROR: preprocessor test failed."
      if [[ -s $DETAIL_LOG ]]; then
        echo "ERROR: See $DETAIL_LOG for details. Last four lines are as follows:"
        tail -4 $DETAIL_LOG
      else
        rm -f $DETAIL_LOG
      fi
      return 3;
    fi
    rm -f $DETAIL_LOG

    # Strip our delimiters and any whitespace introduced by the preprocessor
    local space=$' \t\n\v\f\r' # [:space:] == space, tab, newline, vertical tab, form feed, carriage return
    local delim="[$space]*\+[$space]*"
    [[ "$output" =~ ${token1}${delim}(${regex})${delim}${token2} ]] || return $?

    UPCXX_REMATCH=("${BASH_REMATCH[@]:1}")  # "shifted" to remove full match with our delimeters
    return 0
}

# Wrapper for cpp_extract_expr() operating on gasnet_portable_platform.h
#
# Arguments:
#   compiler: literal 'CC' or 'CXX'
#       expr: string to be expanded between a pair of delimiters
#      regex: bash regular expression used to extract the actual result
#
cpp_extract_pp_expr() {
    local gasnet_src='none'
    if [[ "$GASNET_TYPE" == 'source' ]]; then
        # $GASNET is the source directory
        gasnet_src="$GASNET"
    elif [[ $(grep ^TOP_SRCDIR $GASNET/Makefile) =~ TOP_SRCDIR( *)=( *)(.*) ]]; then
        # Must find the source directory in $GASNET/Makefile
        gasnet_src="${BASH_REMATCH[3]}"
    fi
    local gasnet_includes="-I${gasnet_src}/other"
    cpp_extract_expr "$1" "$2" "$3" "$gasnet_includes" '#include "gasnet_portable_platform.h"'
}

# For probing for lowest acceptable (for its libstdc++) g++ version.
# These are defaults, which may be overridden per-platform (such as Cray XC).
MIN_GNU_MAJOR=6
MIN_GNU_MINOR=4
MIN_GNU_PATCH=0
MIN_GNU_STRING='6.4'

# Run CC or CXX to determine what __GNUC__,__GNUC_MINOR__,__GNUC_PATCHLEVEL__ it reports.
# First argument is CC or CXX (literal)
# Second (optional) argument is an actual compiler command to use in place of CC or CXX
# Results are in gnu_version and gnu_{major,minor,patch} upon return.
# Returns:
#   0 - success
#   1 - identified too-low version
#   other - failed to identify version
check_gnu_version() {
    local cpp_expr='_CONCAT6(v,__GNUC__,_,__GNUC_MINOR__,_,__GNUC_PATCHLEVEL__)'
    local bash_re='v([0-9]+)_([0-9]+)_([0-9]+)'
    if ! cpp_extract_expr $1 "$cpp_expr" "$bash_re"; then
        echo "ERROR: regex match failed probing \$$1 for GNUC version"
        return 2
    fi
    gnu_major=${UPCXX_REMATCH[1]}
    gnu_minor=${UPCXX_REMATCH[2]}
    gnu_patch=${UPCXX_REMATCH[3]}
    gnu_version="$gnu_major.$gnu_minor.$gnu_patch"
    return $(( (    gnu_major*1000000 +     gnu_minor*1000 +     gnu_patch) <
               (MIN_GNU_MAJOR*1000000 + MIN_GNU_MINOR*1000 + MIN_GNU_PATCH) ))
}

# extract gcc-name or gxx-name from an Intel compiler
# on success, returns zero and yields the string value on stdout
#             may emit warning(s) on stderr
# on failure, returns non-zero and yields an error message on stdout
#
# precondition: $gnu_version must be set as by a preceeding 'check_gnu_version CXX'
get_intel_gcc_name_option() {
    case $1 in
         CC) local suffix=c   option=-gcc-name exe=gcc compiler="$CC $CFLAGS" ;;
        CXX) local suffix=cpp option=-gxx-name exe=g++ compiler="$CXX $CXXFLAGS";;
          *) echo Internal error; exit 1;;
    esac
    trap "rm -f conftest.$suffix conftest.o" RETURN
    echo >conftest.$suffix
    local output result
    output="$(eval $compiler -v -o conftest.o -c conftest.$suffix 2>&1)"
    if [[ $? -ne 0 ]]; then
        echo "could not run $1 to extract verbose output"
        return 1
    fi
    # The greediness of '.*' ensures we find the last match
    if [[ $output =~ (.*[ \'\"=]$option=([^ \'\"]+)) ]]; then
        match="${BASH_REMATCH[2]}"
        if [[ $match =~ ^/ ]]; then
            # absolute path
            result="$match"
        elif [[ $match =~ / ]]; then
            # relative path
            result=$(cd $(dirname "$match") && pwd -P)/$(basename "$match")
        else
            # bare name to search in $PATH
            result=$(type -p $match)
        fi
        if [[ ! $result =~ ^/ || ! -x "$result" ]]; then
            echo "failed to convert '$match' to an absolute path to a compiler executable"
            return 1
        fi
    else
        result="$(type -p $exe 2>/dev/null)"
        if [[ $? -ne 0 ]]; then
            echo "failed to locate $exe in \$PATH"
            return 1
        fi
    fi
    local icpc_gnu_version=$gnu_version
    check_gnu_version CXX "$result"
    if [[ $? -gt 1 ]]; then
        echo "could not run $result to extract GNU version"
        return 1
    elif [[ $icpc_gnu_version != $gnu_version ]]; then
        local msg="$result did not report expected version $icpc_gnu_version (got $gnu_version)"
        # Ick!  Older icpc seen to sometimes misreport GNUC info for /usr/bin/{gcc,g++}
        # So, error-out only if not testing the system compiler or the major versions dont't match
        if [[ $result != "/usr/bin/$exe" || ${icpc_gnu_version%%.*} != ${gnu_version%%.*} ]]; then
            echo "$msg"
            return 1
        else
            echo -e "WARNING: $msg\n" >&2
        fi
    fi
    case $1 in
         CC) echo "-gcc-name=$result";;
        CXX) # Ick!  Need both -gcc-name and -gxx-name to be completely effective on some systems.
             # Note we do not xform g++ to gcc in $result, since icpc doesn't seem to care.
             # That is a good thing, since such a transformation would be fragile.
             echo "-gxx-name=$result -gcc-name=$result";;
    esac
}

# Checks and flags specific to Intel compilers with GCC toolchain
# + Verify the GCC toolchain meets the minimum libstdc++ version
# + Determine the flags needed to ensure we always use the same toolchain as
#   probed at configure time, even if end-user has another in $PATH at
#   application compile time.
check_intel_toolchain_gcc() {
    check_gnu_version CXX
    case $? in
        0)  # OK
            ;;
        1)  # Too low
            echo "ERROR: UPC++ with Intel compilers requires use of g++ version $MIN_GNU_STRING or" \
                 "newer, but version $gnu_version was detected."
            echo 'Please do `module load gcc`, or otherwise ensure a new-enough g++ is used by the' \
                 'Intel C++ compiler.'
            if [[ ! -d /opt/cray ]]; then # not assured of trustworthy intel environment module(s)
                echo 'An explicit `-gxx-name=...` and/or `-gcc-name=...` option in the value of' \
                     '$CXX or $CXXFLAGS may be necessary.  Information on these options is available' \
                     'from Intel'\''s \ documentation, such as `man icpc`.'
            fi
            return 1
            ;;
        *)  # Probe failed
            return 1
            ;;
    esac

    # Find the actual g++ in use
    # Append to CXXFLAGS unless already present there, or in CXX
    local gxx_name # do not merge w/ assignment or $? is lost!
    gxx_name=$(get_intel_gcc_name_option CXX)
    if [[ $? -ne 0 ]]; then
        echo "ERROR: $gxx_name"
        if [[ ! -d /opt/cray ]]; then # not assured of trustworthy intel environment module(s)
          echo 'Unable to determine the g++ in use by $CXX.  An explicit `-gxx-name=...` and/or' \
               '`-gcc-name=...` option in the value of $CXX or $CXXFLAGS may be necessary.  Information'\
               'on these options is available from Intel'\''s documentation, such as `man icpc`.'
        fi
        return 1
    fi
    if [[ -n $gxx_name && ! "$CXX $CXXFLAGS " =~ " $gxx_name " ]]; then
        CXXFLAGS+="${CXXFLAGS+ }$gxx_name"
    fi

    # same for C compiler, allowing (gasp) that it might be different
    # note that no floor is imposed ($? = 0,1 both considered success)
  if [[ $CCVERS =~ ( \(ICC\) ) ]]; then  # skip probe of $CC if not Intel C
    check_gnu_version CC
    if [[ $? -gt 1 ]]; then
        return 1   # error was already printed
    fi
    local gcc_name # do not merge w/ assignment or $? is lost!
    gcc_name=$(get_intel_gcc_name_option CC)
    if [[ $? -ne 0 ]]; then
        echo "ERROR: $gcc_name"
        if [[ ! -d /opt/cray ]]; then # not assured of trustworthy intel environment module(s)
            echo 'Unable to determine the gcc in use by $CC.  An explicit `-gcc-name=...` option' \
                 'in the value of $CC or $CFLAGS may be necessary.  Information on this option is' \
                 'available from Intel'\''s documentation, such as `man icc`.'
        fi
        return 1
    fi
    if [[ -n $gcc_name && ! "$CC $CFLAGS " =~ " $gcc_name " ]]; then
        CFLAGS+="${CFLAGS+ }$gcc_name"
    fi
  fi
}

# Checks and flags specific to Intel compilers with Clang toolchain
check_intel_toolchain_clang() {
    : # TODO?  This is a stub
    # If support for multiple Xcode installs becomes a requirement, then
    # the equivalent of get_intel_gcc_name_option() will be needed.
}

# checks specific to Intel compilers:
check_intel_compiler() {
   case $KERNEL in
     Darwin)
       check_intel_toolchain_clang
       ;;
     Linux)
       check_intel_toolchain_gcc
       ;;
   esac
}

check_pgi_compiler() {
    local bash_re='(([1-9][0-9]?)([0-9][0-9])([0-9][0-9]))' # 5 or 6 decimal digits
    if ! cpp_extract_expr CXX '__pgnu_vsn' "$bash_re"; then
        echo "WARNING: failed to probe '$CXX' for underlying GNUC/libstdc++ version." \
             "Validation of libstdc++ version has been skipped."
        return 0
    fi
    if  (( ${UPCXX_REMATCH[1]} <
           (MIN_GNU_MAJOR*10000 + MIN_GNU_MINOR*100 + MIN_GNU_PATCH) )); then
        ver_string="${UPCXX_REMATCH[2]}.${UPCXX_REMATCH[3]#0}.${UPCXX_REMATCH[4]#0}"
        echo "ERROR: UPC++ with PGI and NVHPC compilers requires use of g++ version $MIN_GNU_STRING or" \
             "newer, but version $ver_string was detected."
        return 1
    fi
    return 0
}

# Determine if compiler families match
check_family_match() {
    if ! cpp_extract_pp_expr CXX PLATFORM_COMPILER_FAMILYNAME '[A-Z]+'; then
        echo "ERROR: regex match failed probing '$CXX' for C++ compiler family"
        return 4
    fi
    local cxx_family="${UPCXX_REMATCH[0]}"

    if ! cpp_extract_pp_expr CC PLATFORM_COMPILER_FAMILYNAME '[A-Z]+'; then
        echo "ERROR: regex match failed probing '$CC' for C compiler family"
        return 4
    fi
    local cc_family="${UPCXX_REMATCH[0]}"

    [[ $cxx_family = $cc_family ]] && return 0 || return 1
}

# Determine if compiler versions match
# Meaningless if family does not match
check_version_match() {
    if ! cpp_extract_pp_expr CXX '(PLATFORM_COMPILER_VERSION)' '\(.*\)'; then
        echo "ERROR: regex match failed probing '$CXX' for C++ compiler version"
        return 4
    fi

    # strips any possible suffixes from integer constants:
    local cxx_version="${UPCXX_REMATCH[0]//[uUlL]}"

    if ! cpp_extract_pp_expr CC '(PLATFORM_COMPILER_VERSION)' '\(.*\)'; then
        echo "ERROR: regex match failed probing '$CC' for C compiler version"
        return 4
    fi

    # strips any possible suffixes from integer constants:
    local cc_version="${UPCXX_REMATCH[0]//[uUlL]}"

    # Use arithmetic evaluation to allow for minor differences in the actual expressions
    return $(( $cxx_version != $cc_version ))  # equality returns 0 (success)
}

# check whether $CXX might be a C compiler
check_maybe_c_compiler() {
    local c_compiler=
    case $(basename "$CXX") in
        gcc|gcc-*|clang|icc|icx|pgcc|mpicc|cc) c_compiler=1;;
    esac
    if test -n "$c_compiler" ; then
        echo "ERROR: It looks like CXX=$CXX may be a C compiler."\
             "Please use a C++ compiler instead."
    fi
}

# compile_check(): checks that $CXX can compile C++ code and is
#   link-compatible with $CC.
compile_check() {
    local DETAIL_LOG=config-detail.log
    rm -f $DETAIL_LOG
    # check if we need to inject -std=c++11 flag
    if ! cpp_extract_expr CXX '__cplusplus' '([1-9][0-9]+)[lL]?'; then
        echo "ERROR: regex match failed probing \$CXX for C++ standard version"
        return 4
    fi
    local CXXSTDFLAG=
    if (( ${UPCXX_REMATCH[1]} < 201103 )); then
        CXXSTDFLAG="-std=c++11"
    fi
    # check C compilation
    trap "rm -f conftest-std.cpp conftest-cc.c conftest-cc.o conftest-cxx.cpp conftest-cxx.o conftest.o" RETURN
    cat >conftest-cc.c <<_EOF
      #include <math.h>
      #include <stdio.h>
      #include <stdlib.h>

      extern int cppextfunc(double);

      int cfunc(double x) {
        printf("[from C] cfunc(%f)\n", x);
        double *ptr = malloc(sizeof(double)); // okay in C, not in C++
        *ptr = sqrt(x);
        int res = abs(cppextfunc(*ptr));
        free(ptr);
        return res;
      }
_EOF
    if ! (set -x; eval $CC $CFLAGS -c conftest-cc.c) >> $DETAIL_LOG 2>&1 ; then
        echo "ERROR: CC=$CC failed to compile test C file"
        echo "ERROR: See $DETAIL_LOG for details. Last four lines are as follows:"
        tail -4 $DETAIL_LOG
        return 1
    fi
    # check C++ compilation
    cat >conftest-cxx.cpp <<_EOF
      #include <iostream>
      #include <new>
      #include <tuple>
      #include <type_traits>
      #include <vector>

      extern "C" int cfunc(double);

      extern "C" int cppextfunc(double x) {
        return static_cast<int>(x);
      }

      namespace cppnamespace {
        template<typename T>
        auto func(T&& x) -> typename std::enable_if<std::is_same<T,int>::value,int>::type {
          if (x != 0) throw 0;
          return 0;
        }

        template<typename T>
        auto func(T&&) -> typename std::enable_if<!std::is_same<T,int>::value,int>::type {
          return 1;
        }
      }

      int main() {
        std::cout << "[from C++] cfunc(7.3)" << std::endl;
        std::cout << cfunc(7.3) << std::endl;
        try {
          std::cout << cppnamespace::func(3) << std::endl;
        } catch (int i) {
          std::cout << "caught " << i << std::endl;
        }
        std::cout << cppnamespace::func(3.1) << std::endl;
        auto lambda = [](std::vector<double> &vec) {
                        return std::make_tuple(vec[0], vec.size());
                      };
        std::vector<double> v = { 1.1, -2.2, 3.3 };
        v.~vector();
        std::vector<double> *ptr = new(&v) std::vector<double>({ -4.4, 5.5 });
        std::tuple<double, std::vector<double>::size_type> t = lambda(*ptr);
        std::cout << "(" << std::get<0>(t) << "," << std::get<1>(t) << ")" << std::endl;
        std::tuple<> empty;
        auto t2 = std::tuple_cat(t, empty);
        double d;
        std::vector<double>::size_type s;
        std::tie(d, s) = t2;
        std::cout << d << " " << s << std::endl;
        return 0;
      }
_EOF
    if ! (set -x; eval $CXX $CXXFLAGS $CXXSTDFLAG -c conftest-cxx.cpp) >> $DETAIL_LOG 2>&1 ; then
        echo "ERROR: CXX=$CXX failed to compile test C++ file"
        echo "ERROR: See $DETAIL_LOG for details. Last four lines are as follows:"
        tail -4 $DETAIL_LOG
        check_maybe_c_compiler
        return 2
    fi
    if ! (set -x; eval $CXX $CXXFLAGS $CXXSTDFLAG -o conftest.o conftest-cc.o conftest-cxx.o -lm) >> $DETAIL_LOG 2>&1 ; then
        echo "ERROR: CXX=$CXX failed to link object files produced by CC=$CC and CXX=$CXX"
        echo "ERROR: See $DETAIL_LOG for details. Last four lines are as follows:"
        tail -4 $DETAIL_LOG
        check_maybe_c_compiler
        return 3
    fi
    # actually run the test if not cross compiling
    if test -z "$UPCXX_CROSS" && ! (set -x; ./conftest.o) >> $DETAIL_LOG 2>&1 ; then
        echo "ERROR: Test program successfully compiled with CC=$CC and CXX=$CXX but failed to"\
             "run correctly. The required dynamic libraries may be missing."
        echo "ERROR: See $DETAIL_LOG for details. Last four lines are as follows:"
        tail -4 $DETAIL_LOG
        return 5
    fi
    rm -f $DETAIL_LOG
}

# platform_sanity_checks(): defaults $CC and $CXX if they are unset
#   validates the compiler and system versions for compatibility
#   setting UPCXX_INSTALL_NOCHECK=1 disables this function *completely*.
#   That includes disabling many side-effects such as platform-specific
#   defaults, conversion of CC and CXX to full paths, and addition of
#   certain options to {C,CXX}FLAGS.
platform_sanity_checks() {
    if test -z "$UPCXX_INSTALL_NOCHECK" ; then
        local KERNEL=`uname -s 2> /dev/null`
        local KERNEL_GOOD=
        if test Linux = "$KERNEL" || test Darwin = "$KERNEL" ; then
            KERNEL_GOOD=1
        fi
        if [[ $UPCXX_CROSS =~ ^cray-aries- ]]; then
            if test -n "$CRAY_PRGENVCRAY" && expr "$CRAY_CC_VERSION" : "^[78]" > /dev/null; then
                echo 'ERROR: UPC++ on Cray XC with PrgEnv-cray requires cce/9.0 or newer.'
                exit 1
            elif test -n "$CRAY_PRGENVCRAY" && expr x"$CRAY_PE_CCE_VARIANT" : "xCC=Classic" > /dev/null; then
                echo 'ERROR: UPC++ on Cray XC with PrgEnv-cray does not support the "-classic" compilers such as' \
                     $(grep -o 'cce/[^:]*' <<<$LOADEDMODULES)
                exit 1
            elif test -z "$CRAY_PRGENVGNU$CRAY_PRGENVINTEL$CRAY_PRGENVCRAY"; then
                echo 'WARNING: Unsupported PrgEnv.' \
                     'UPC++ on Cray XC currently supports PrgEnv-gnu, intel or cray. ' \
                     'Please do: `module switch PrgEnv-[CURRENT] PrgEnv-[FAMILY]`' \
                     'for your preferred compiler FAMILY.'
                # currently neither GOOD nor BAD
            fi
            CC=${CC:-cc}
            CXX=${CXX:-CC}
            MIN_GNU_MAJOR=7
            if test -z "$CRAY_PRGENVINTEL"; then
              MIN_GNU_MINOR=1
            else
              # Old icpc with new g++ reports 0 for __GNUC_MINOR__.
              # Since GCC releases START at MINOR=1 (never 0) we can safely
              # allow a MINOR==0 knowing the real value is at least 1.
              MIN_GNU_MINOR=0
            fi
            MIN_GNU_PATCH=0
            MIN_GNU_STRING='7.1'
        elif test "$KERNEL" = "Darwin" ; then # default to XCode clang
            CC=${CC:-/usr/bin/clang}
            CXX=${CXX:-/usr/bin/clang++}
        else
            CC=${CC:-gcc}
            CXX=${CXX:-g++}
        fi
        local ARCH=`uname -m 2> /dev/null`
        local ARCH_GOOD=
        local ARCH_BAD=
        if test x86_64 = "$ARCH" ; then
            ARCH_GOOD=1
        elif test ppc64le = "$ARCH" ; then
            ARCH_GOOD=1
        elif test aarch64 = "$ARCH" ; then
            ARCH_GOOD=1
            # ARM-based Cray XC not yet tested
            if test -n "$CRAY_PEVERSION" ; then
              ARCH_GOOD=
            fi
        elif expr "$ARCH" : 'i.86' >/dev/null 2>&1 ; then
            ARCH_BAD=1
        fi

        # absify compilers, checking they exist
        cxx_exec=$(check_tool_path "$CXX")
        if [[ $? -ne 0 ]]; then
            echo "ERROR: CXX='${CXX%% *}' $cxx_exec"
            exit 1
        fi
        CXX=$cxx_exec
        cc_exec=$(check_tool_path "$CC")
        if [[ $? -ne 0 ]]; then
            echo "ERROR: CC='${CC%% *}' $cc_exec"
            exit 1
        fi
        CC=$cc_exec
        if test -z "$UPCXX_INSTALL_QUIET" ; then
            echo $CXX
            eval $CXX --version 2>&1 | grep -v 'warning #10315'
            echo $CC
            eval $CC --version 2>&1 | grep -v 'warning #10315'
            echo " "
        fi

        local CXXVERS=`eval $CXX --version 2>&1`
        local CCVERS=`eval $CC --version 2>&1`
        local COMPILER_BAD=
        local COMPILER_GOOD=
        local EXTRA_RECOMMEND=
        if echo "$CXXVERS" | egrep 'Apple LLVM version [1-7]\.' 2>&1 > /dev/null ; then
            COMPILER_BAD=1
        elif echo "$CXXVERS" | egrep 'Apple (LLVM|clang) version ([8-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
            COMPILER_GOOD=1
        elif echo "$CXXVERS" | egrep '(PGI|NVIDIA) Compilers and Tools'  > /dev/null ; then
            if egrep ' +20\.[5-8]-' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # Ex: "pgc++ (aka nvc++) 20.7-0 LLVM 64-bit target on x86-64 Linux -tp nehalem"
               # Release 20.7 is known bad (see GASNet bug 4115).
               # However, 20.4 (from PGI) and 20.9 (from Nvidia) are known good.
               # We conservatively ban 20.[5-8] even though only 20.7 is known to exist.
               COMPILER_BAD=1
            elif [[ "$ARCH,$KERNEL" = 'x86_64,Linux' ]] &&
                 egrep ' +(19\.[3-9]|19\.1[0-9]|[2-9][0-9]\.[0-9]+)-' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # Ex: "pgcc 19.10-0 LLVM 64-bit target on x86-64 Linux -tp nehalem "
               # 19.3 and newer "GOOD"
               COMPILER_GOOD=1
            elif [[ "$ARCH,$KERNEL" = 'ppc64le,Linux' ]] &&
                 egrep ' +(19\.[3-9]|19\.1[0-9]|[2-9][0-9]\.[0-9]+)-' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # Ex: "pgcc (aka pgcc18) 20.1-0 linuxpower target on Linuxpower"
               # 19.3 and newer "GOOD"
               COMPILER_GOOD=1
            elif [[ "$ARCH,$KERNEL" = 'aarch64,Linux' ]] ; then
               : # Not yet claiming support on aarch64, but also not BAD
            else
               # Unsuported platform or version
               COMPILER_BAD=1
            fi
            if [[ $UPCXX_CROSS =~ ^cray-aries- ]]; then
               # PrgEnv-pgi: currently neither GOOD nor BAD due to lack of testing
               # However, if logic above identified a bad version, we'll preserve that.
               unset COMPILER_GOOD
            elif [[ $LMOD_FAMILY_PRGENV = PrgEnv-nvidia || $LMOD_FAMILY_PRGENV = PrgEnv-nvhpc ]]; then
               # HPE Cray EX (Shasta) only validated for 21.9 and newer
               if ! egrep ' +(21\.9|21\.1[0-9]|2[2-9]\.[0-9]+|[3-9][0-9]\.[0-9]+)-' <<<"$CXXVERS" 2>&1 >/dev/null ; then
                  unset COMPILER_GOOD
               fi
            fi
            if (( ! COMPILER_BAD )); then
               check_pgi_compiler || exit 1
            fi
        elif echo "$CXXVERS" | egrep 'IBM XL'  > /dev/null ; then
            COMPILER_BAD=1
        elif echo "$CXXVERS" | egrep 'Free Software Foundation' 2>&1 > /dev/null &&
             ! check_gnu_version CXX &> /dev/null; then
            COMPILER_BAD=1
        elif [[ "$KERNEL" = 'Darwin' ]] && \
             echo "$CXXVERS" | egrep ' +\(ICC\) +(2021\.[3-9]|202[2-9]\.)' 2>&1 > /dev/null ; then
	    # Ex: icpc (ICC) 2021.3.0 20210609
            check_intel_compiler || exit 1
            #COMPILER_GOOD=1 Not yet
        elif [[ "$CRAY_PRGENVINTEL$KERNEL" = 'Linux' ]] && \
             echo "$CXXVERS" | egrep ' +\(ICC\) +(17\.0\.[2-9]|1[89]\.|(20)?2[0-9]\.)' 2>&1 > /dev/null ; then
	    # Ex: icpc (ICC) 18.0.1 20171018
            check_intel_compiler || exit 1
            COMPILER_GOOD=1
        elif test -n "$CRAY_PRGENVINTEL" && \
             echo "$CXXVERS" | egrep ' +\(ICC\) +(18\.0\.[1-9]|19\.|(20)?2[0-9]\.)' 2>&1 > /dev/null ; then
            check_intel_compiler || exit 1
            COMPILER_GOOD=1
        elif echo "$CXXVERS" | egrep ' +\(ICC\) ' 2>&1 > /dev/null ; then
            check_intel_compiler
            if [[ $? -ne 0 ]]; then
              if [[ -d /opt/cray ]]; then
                echo 'ERROR: Your Intel compiler is too old, please `module swap intel intel` (or' \
                     'similar) to load a supported version'
                exit 1
              else
                # continue past messages for a too-old libstdc++ and proceed to
                # warning about unsupported compiler, with a line break between
                echo
              fi
            fi
        elif [[ $CXXVERS =~ (oneAPI .* (20[0-9][0-9])\.([0-9]+)\.([0-9]+)) ]]; then
            if ((BASH_REMATCH[2]*10000 + BASH_REMATCH[3]*100 + BASH_REMATCH[4] >= 20210102 )); then
              COMPILER_GOOD=1
            fi
            # older versions unknown for now
        elif echo "$CXXVERS" | egrep 'Free Software Foundation' 2>&1 > /dev/null &&
             check_gnu_version CXX &> /dev/null; then
            COMPILER_GOOD=1
            # Arm Ltd's gcc not yet tested
            if test aarch64 = "$ARCH" && echo "$CXXVERS" | head -1 | egrep ' +\(ARM' 2>&1 > /dev/null ; then
              COMPILER_GOOD=
            fi
        elif echo "$CXXVERS" | egrep 'clang version [23]' 2>&1 > /dev/null ; then
            COMPILER_BAD=1
        elif test x86_64 = "$ARCH" && echo "$CXXVERS" | egrep 'clang version ([4-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
            COMPILER_GOOD=1
            if [[ $LMOD_FAMILY_PRGENV = PrgEnv-aocc ]]; then
               # HPE Cray EX (Shasta) only validated for aocc/3.1.0 and newer
               # NOTE: 3.2 was end of 3.x series, but we'll accept up to 3.9 here
               if ! egrep 'AOCC_(3\.[1-9]|[4-9]\.|[1-9][0-9]\.)' <<<"$CXXVERS" 2>&1 >/dev/null ; then
                  unset COMPILER_GOOD
               fi
            elif [[ $LMOD_FAMILY_PRGENV = PrgEnv-amd ]]; then
               # HPE Cray EX (Shasta) only validated for amd/4.2.0 and newer
               # NOTE: 4.5 was end of 4.x series, but we'll accept up to 4.9 here
               if ! egrep 'roc-(4\.[2-9]|[5-9]\.|[1-9][0-9]\.)' <<<"$CXXVERS" 2>&1 >/dev/null ; then
                  unset COMPILER_GOOD
               fi
            elif egrep 'AOCC_(1\.|2\.[0-2])' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # AOCC older than 2.3 has not been validated
               unset COMPILER_GOOD
            elif grep 'AOCC\.LLVM\.1' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # AOCC 1.x is known bad
               unset COMPILER_GOOD
               COMPILER_BAD=1
            fi
        elif test ppc64le = "$ARCH" && echo "$CXXVERS" | egrep 'clang version ([5-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
	    # Issue #236: ppc64le/clang support floor is 5.x. clang-4.x/ppc has correctness issues and is deliberately left "unvalidated"
            COMPILER_GOOD=1
        elif test aarch64 = "$ARCH" && echo "$CXXVERS" | egrep 'clang version ([4-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
            COMPILER_GOOD=1
            # Arm Ltd's clang not yet tested
            if echo "$CXXVERS" | egrep '^Arm C' 2>&1 > /dev/null ; then
              COMPILER_GOOD=
            fi
        fi

        check_family_match
        local COMPILER_MISMATCH_RC=$?
        local COMPILER_MISMATCH=
        if (( COMPILER_MISMATCH_RC )); then
            COMPILER_MISMATCH='families'
        else
            check_version_match
            COMPILER_MISMATCH_RC=$?
            if (( COMPILER_MISMATCH_RC )); then
              COMPILER_MISMATCH='versions'
            fi
        fi
        if (( COMPILER_MISMATCH_RC )); then
            if (( $UPCXX_ALLOW_COMPILER_MISMATCH )); then
                if (( COMPILER_MISMATCH_RC > 1 )); then
                    warnings+="\nWARNING: The probe for compiler $COMPILER_MISMATCH failed (see above).\n"
                else
                    warnings+="\nWARNING: CXX and CC report different $COMPILER_MISMATCH (see above).\n"
                fi
                warnings+="WARNING: Therefore, this configuration is officially unsupported.\n"
            else
                echo
                if (( COMPILER_MISMATCH_RC > 1 )); then
                    echo 'ERROR: UPC++ requires that the C++ and C compilers match, but the probe for'
                    echo "ERROR: compiler $COMPILER_MISMATCH failed (see above)."
                    echo 'ERROR: Please that ensure you are configuring with valid (and matched)'
                    echo 'ERROR: values for `--with-cxx=...` and `--with-cc=...`.'
                else
                    echo 'ERROR: UPC++ requires that the C++ and C compilers match, but the compilers'
                    echo "ERROR: detected by configure (see above) report different $COMPILER_MISMATCH."
                    echo 'ERROR: In most cases, configuring UPC++ using matched values for both'
                    echo 'ERROR: `--with-cxx=...` and `--with-cc=...` will resolve this problem.'
                fi
                echo 'ERROR: See INSTALL.md for the full list of supported compilers.'
                echo 'ERROR: Alternatively, configuring with `--enable-allow-compiler-mismatch`'
                echo 'ERROR: will disable this sanity check, but result in an unsupported build.'
                exit 1
            fi
        fi

        local COMPILER_FAIL=
        if ! compile_check ; then
            COMPILER_FAIL=1
        fi

        local RECOMMEND
        read -r -d '' RECOMMEND<<'EOF'
We recommend one of the following C++ compilers (or any later versions where no end-of-range is given):
           Linux on x86_64:   g++ 6.4.0, LLVM/clang 4.0.0, PGI 19.3 through 20.4 (inclusive),
                              NVIDIA HPC SDK 20.9, Intel C 17.0.2, Intel oneAPI compilers 2021.1.2,
                              AMD AOCC 2.3.0
           Linux on ppc64le:  g++ 6.4.0, LLVM/clang 5.0.0, PGI 19.3 through 20.4 (inclusive),
                              NVIDIA HPC SDK 20.9
           Linux on aarch64:  g++ 6.4.0, LLVM/clang 4.0.0
           macOS on x86_64:   g++ 6.4.0, Xcode/clang 8.0.0
           Cray XC systems:   PrgEnv-gnu with gcc/7.1.0 environment module loaded
                              PrgEnv-intel with Intel C 18.0.1 and gcc/7.1.0 environment modules loaded
                              PrgEnv-cray with cce/9.0.0 environment module loaded
                              ALCF's PrgEnv-llvm/4.0
           HPE Cray EX:       PrgEnv-gnu with gcc/10.3.0 environment module loaded
                              PrgEnv-cray with cce/12.0.0 environment module loaded
                              PrgEnv-amd with amd/4.2.0 environment module loaded
                              PrgEnv-aocc with aocc/3.1.0 environment module loaded
                              PrgEnv-nvidia with nvidia/21.9 environment module loaded
                              PrgEnv-nvhpc with nvhpc/21.9 environment module loaded
                              PrgEnv-intel with intel/2023.3.1 environment module loaded
EOF
        if test -n "$ARCH_BAD" ; then
            echo "ERROR: This version of UPC++ does not support the '$ARCH' architecture."
            echo "ERROR: $RECOMMEND$EXTRA_RECOMMEND"
            exit 1
        elif test -n "$COMPILER_BAD" ; then
            echo 'ERROR: Your C++ compiler is known to lack the support needed to build UPC++. '\
                 'Please set $CC and $CXX to point to a newer C/C++ compiler suite.'
            echo "ERROR: $RECOMMEND$EXTRA_RECOMMEND"
            exit 1
        elif test -n "$COMPILER_FAIL" ; then
            echo 'ERROR: Your C and C++ compilers failed to compile and link C/C++ code. '\
                 'Please set $CC and $CXX to ABI-compatible C and C++ compilers, respectively.'
            echo "ERROR: $RECOMMEND$EXTRA_RECOMMEND"
            exit 1
        elif test -z "$COMPILER_GOOD" || test -z "$KERNEL_GOOD" || test -z "$ARCH_GOOD" ; then
            echo 'WARNING: Your C++ compiler or platform has not been validated to run UPC++'
            echo "WARNING: $RECOMMEND$EXTRA_RECOMMEND"
        fi
    fi
    return 0
}

platform_settings() {
   local KERNEL=`uname -s 2> /dev/null`
   case "$KERNEL" in
     Linux)
       LDFLAGS="-Wl,--build-id ${LDFLAGS}"
       ;;
     *)
       ;;
   esac
   if [[ $UPCXX_CROSS =~ ^cray-aries- ]]; then
     # ~8kb is experimentally the best max AM threshold for RPC eager protocol on aries
     GASNET_CONFIGURE_ARGS="--with-aries-max-medium=8128 ${GASNET_CONFIGURE_ARGS}"
   fi
}

