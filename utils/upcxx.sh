#!/bin/bash

function error {
  echo "upcxx: error:" "$@" >&2
  exit 1
}

doverbose=0
function verbose {
  if [[ $doverbose == 1 ]]; then
    echo "upcxx:" "$@" >&2
  fi
}

function set_upcxx_var {
  local var="$1"
  local val="$2"
  if [[ ${BASH_VERSINFO[0]} -ge 4 ]] ; then 
    # use case modification operators when avail, for efficiency
    var="${var^^}"
    val="${val,,}"
  else # legacy bash (eg macOS), fork additional processes
    var=`echo "$var" | awk '{ print toupper($0) }'`
    val=`echo "$val" | awk '{ print tolower($0) }'`
  fi
  # per-var processing
  case $var in
    UPCXX_CODEMODE)
      # apply some (deliberately undocumented) "fuzzy" leniency to value spelling
      case $val in
        opt|o|o[1-9]) val=opt ; ;;
        debug|g|o0) val=debug ;;
        *) if [[ $doversion || $dohelp ]] ; then
             val=opt # treat invalid settings as "opt" for the purposes of -help/-version
           else
             error "Unrecognized -codemode value, must be 'opt' or 'debug'" 
           fi
           ;;
      esac
      codemode_override=1
    ;;
  esac
  eval $var='$val'
}

if [[ "$UPCXX_META" != 'BUILDDIR' ]]; then
  if ! test -x "$UPCXX_META" ; then
    error UPCXX_META=$UPCXX_META not found
  fi
  prefix="${UPCXX_META%/*/*}" # strip the last two components in the path
  # in builddir need to strip one additional component
  prefix="${prefix%/upcxx.assert*.optlev*.dbgsym*.gasnet_*.*}"
  if ! test -d "$prefix" ; then
    error install prefix $prefix not found
  fi
fi

UPCXX_NETWORK=${UPCXX_NETWORK:-$UPCXX_GASNET_CONDUIT} # backwards-compat
export UPCXX_NETWORK
export UPCXX_THREADMODE
export UPCXX_CODEMODE

dolink=1
doversion=
doinfo=
dodebug=
purgeoption=
doopt=
codemode_override=
docc=
docxx=
shopt -u nocasematch # ensure case-sensitive match below
shopt -s extglob # enable extended regexp below
for ((i = 1 ; i <= $# ; i++)); do
  arg="${@:i:1}"
  case $arg in 
    +(-)network=*|+(-)threadmode=*|+(-)codemode=*)
      var="${arg%%=*}"
      var="${var##+(-)}"
      val="${arg#*=}"
      eval set_upcxx_var UPCXX_"$var" "$val"
      # swallow current arg
      set -- "${@:1:i-1}" "${@:i+1}"
      i=$((i-1))
    ;;
    +(-)network|+(-)threadmode|+(-)codemode)
      var="${arg##+(-)}"
      val="${@:i+1:1}"
      eval set_upcxx_var UPCXX_"$var" "$val"
      # swallow current and next arg
      set -- "${@:1:i-1}" "${@:i+2}"
      i=$((i-1))
    ;;
    -purge-option=*)
      purgeoption="${arg#*=}"
      # swallow current arg
      set -- "${@:1:i-1}" "${@:i+1}"
      i=$((i-1))
    ;;
    -Wc,*) # -Wc,anything : anything is passed-thru uninterpreted
      val="${arg#*,}"
      set -- "${@:1:i-1}" "$val" "${@:i+1}"
    ;;
    -E|-c|-S) dolink='' ;;
    -M|-MM) dolink='' ;; # gcc: these imply no compilation or linking (-MD/-MMD deliberately omitted)
    -v|-vv) doverbose=1 ;;
    -V|+(-)version) 
      doversion=1
    ;;
    +(-)info) 
      doinfo=1
    ;;
    +(-)help) 
      dohelp=1
    ;;
    -g0) dodebug='' ;; # -g0 negates -g
    -g)  dodebug=1
      # swallow bare -g to avoid overriding the debug level in our default flags
      set -- "${@:1:i-1}" "${@:i+1}" 
      i=$((i-1))
    ;;
    -g*) dodebug=1 ;;
    -O0) doopt='' ;; # -O0 negates -O
    -O)  doopt=1
      # swallow bare -O to avoid overriding the opt level in our default flags
      set -- "${@:1:i-1}" "${@:i+1}" 
      i=$((i-1))
    ;;
    -O*) doopt=1 ;;
    *.c) docc=1 ;;
    *.cxx|*.cpp|*.cc|*.c++|*.C++) docxx=1 ;;
  esac
done
verbose dolink=$dolink
verbose UPCXX_META=$UPCXX_META

if [[ $codemode_override ]] ; then
  :  # -codemode is highest priority and ignores other args
elif [[ $dodebug && ! $doopt ]] ; then
  UPCXX_CODEMODE=debug
elif [[ $doopt && ! $dodebug ]] ; then
  UPCXX_CODEMODE=opt
elif [[ $UPCXX_CODEMODE ]] ; then
  : # last resort : user environment
  eval set_upcxx_var UPCXX_CODEMODE "$UPCXX_CODEMODE"
elif [[ $doversion || $dohelp ]] ; then
  # we have no codemode indications from anywhere, so just assume opt for the purposes of help/version
  UPCXX_CODEMODE=opt
else
  error "please specify one of the -O or -g options supported by your C++ compiler, otherwise pass -codemode={opt,debug} or set UPCXX_CODEMODE={opt,debug} to select the production or development version of the library."
fi

for var in UPCXX_NETWORK UPCXX_THREADMODE ; do
  eval "[[ -n \"\$$var\" ]] && set_upcxx_var $var \"\$$var\""
done

if [[ $docxx && $docc ]] ; then
  error "please do not specify a mix of C and C++ source files on the same invocation"
elif [[ $docc && $dolink ]] ; then
  error "please compile C language source files separately using -c"
fi

if [[ "$UPCXX_META" == 'BUILDDIR' ]]; then
  for var in UPCXX_CODEMODE UPCXX_NETWORK UPCXX_THREADMODE ; do
    eval "[[ -n "\$$var" ]] && echo $var=\$$var"
  done
  exit 0
fi

for var in UPCXX_CODEMODE UPCXX_NETWORK UPCXX_THREADMODE ; do
  eval verbose $var=\$$var
done

source $UPCXX_META SET
[[ -z "$CC" ]] && error "failure in UPCXX_META=$UPCXX_META"

if [[ -n "$purgeoption" ]] ; then
  # Yuk: BASH regex is horribly broken and non-portable.
  # For details, see: https://stackoverflow.com/questions/9792702/
  # However, we want to use the shell facilities to avoid worse problems with inner quoting
  # The following is NOT perfect, but should be sufficient for the cases we care about
  for var in CC CFLAGS CXX CXXFLAGS CPPFLAGS LDFLAGS LIBS ; do 
    space=' '
    eval $var="\$space\${$var}\$space"    # surround start/end with space to avoid anchors
    eval $var="\${$var// $purgeoption / }" # space is our option boundary
    eval $var="\${$var%% }" # strip the space we added
    eval $var="\${$var## }" # strip the space we added
  done
fi

for var in CC CFLAGS CXX CXXFLAGS CPPFLAGS LDFLAGS LIBS ; do 
  eval verbose "$var: \$$var"
done

EXTRAFLAGS=""
if [[ $dohelp ]] ; then
  cat<<EOF
upcxx is a compiler wrapper that is intended as a drop-in replacement for your
C++ compiler that appends the flags necessary to compile/link with the UPC++ library.
Most arguments are passed through without change to the C++ compiler.
Citing UPC++ in publication? Please see: https://upcxx.lbl.gov/publications

Usage: upcxx [options] file...

upcxx Wrapper Options:
----------------------

 Informational queries (no compilation):
  -help           This message
  -version        Print UPC++ and compiler version information
  -info           Print full configuration info

 UPC++ library configuration:
  -network={ibv|aries|ofi|ucx|smp|udp|mpi}
                   Use the indicated GASNet network backend for communication.
		   The default and availability of backends is system-dependent.
  -codemode={opt|debug}
                   Select the optimized or debugging variant of the UPC++ library.
  -threadmode={seq|par}
                   Select the single-threaded or thread-safe variant of the UPC++ library.

 All other C++ compiler options:
  -Wc,<anything>   <anything> is passed-through uninterpreted to the underlying compiler
  <anything-else>  Passed-through uninterpreted to the underlying compiler

C++ Compiler --help:
--------------------
EOF
  $CXX --help
  exit 0
elif [[ $doversion || $doinfo ]] ; then
 line=--------------------------------------------------------------------
 if [[ ! $UPCXX_VERSION_CLEAN || $doinfo ]] ; then # allow silencing our version prepend
  [[ $doinfo ]] && ( echo $line ; echo "Software Version Info:" ; echo )
  header="$prefix/upcxx.*/include/upcxx/version.hpp $prefix/include/upcxx/version.hpp" # build-tree or installed
  version=$(grep "# *define  *UPCXX_VERSION " ${header} 2>/dev/null| head -1)
  if [[ "$version" =~ ([0-9]{4})([0-9]{2})([0-9]{2}) ]]; then
    version="${BASH_REMATCH[1]}.${BASH_REMATCH[2]#0}.${BASH_REMATCH[3]#0}"
  else
    version=${version##*UPCXX_VERSION }
  fi
  githash=`(cat $prefix/share/doc/upcxx/docs/version.git ) 2> /dev/null`
  gexhash=`(cat $prefix/gasnet.*/share/doc/GASNet/version.git | head -1 ) 2> /dev/null`
  if [[ -z $gexhash ]] ; then
    # version.git is missing (not a GASNet release tarball or git clone)
    # at least report the package version
    gexhash=`(grep -h RELEASE_VERSION $prefix/gasnet.*/config-details.txt | grep -i gasnet | cut -d: -f2 | head -1 ) 2> /dev/null`
    gexhash=${gexhash:+gex-$(echo $gexhash)}
  fi
  if [[ -n $gexhash ]] ; then
    gexhash=" / $gexhash"
  fi
  echo "UPC++ version $version $githash$gexhash"
  echo "Citing UPC++ in publication? Please see: https://upcxx.lbl.gov/publications"
  echo "Copyright (c) 2023, The Regents of the University of California,"
  echo "through Lawrence Berkeley National Laboratory."
  echo "https://upcxx.lbl.gov"
  echo ""
 fi
  $CXX --version
  [[ $doinfo ]] || exit 0
fi
if [[ $doinfo ]] ; then
  echo $line
  echo "Configuration settings:"
  echo
  printf "%-35s %s\n" "prefix:" "$prefix"
  for var in UPCXX_CODEMODE UPCXX_NETWORK UPCXX_THREADMODE ; do
    fmt="%-35s %s\n"
    eval printf \"\$fmt\" $var: \$$var
  done
  for f in \
    $prefix/gasnet.${UPCXX_CODEMODE}/config-details.txt \
    $prefix/gasnet.${UPCXX_CODEMODE}/config-summary.txt \
    ; do
    if [[ -f $f ]] ; then 
      cat $f
    else
      echo Config file not found: $f
    fi
  done
  exit 0
fi

function doit {
  verbose "$@"
  exec "$@"
}
if [[ $docc ]] ; then # C language compilation, for convenience
  doit $CC $CFLAGS $CPPFLAGS "$@"
elif [[ ! $dolink ]] ; then
  doit $CXX $EXTRAFLAGS $CXXFLAGS $CPPFLAGS "$@"
else
  doit $CXX $EXTRAFLAGS $CXXFLAGS $CPPFLAGS $LDFLAGS "$@" $LIBS
fi
error failed to run compiler $CXX

