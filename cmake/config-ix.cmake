if( WIN32 AND NOT CYGWIN )
  # We consider Cygwin as another Unix
  set(PURE_WINDOWS 1)
endif()

include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckCXXSourceCompiles)
include(TestBigEndian)

if( UNIX AND NOT BEOS )
  # Used by check_symbol_exists:
  set(CMAKE_REQUIRED_LIBRARIES m)
endif()

# Helper macros and functions
macro(add_cxx_include result files)
  set(${result} "")
  foreach (file_name ${files})
     set(${result} "${${result}}#include<${file_name}>\n")
  endforeach()
endmacro(add_cxx_include files result)

function(check_type_exists type files variable)
  add_cxx_include(includes "${files}")
  CHECK_CXX_SOURCE_COMPILES("
    ${includes} ${type} typeVar;
    int main() {
        return 0;
    }
    " ${variable})
endfunction()

# include checks
check_include_file_cxx(cxxabi.h HAVE_CXXABI_H)
check_include_file(dirent.h HAVE_DIRENT_H)
check_include_file(dlfcn.h HAVE_DLFCN_H)
check_include_file(errno.h HAVE_ERRNO_H)
check_include_file(execinfo.h HAVE_EXECINFO_H)
check_include_file(fcntl.h HAVE_FCNTL_H)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(limits.h HAVE_LIMITS_H)
check_include_file(malloc.h HAVE_MALLOC_H)
check_include_file(malloc/malloc.h HAVE_MALLOC_MALLOC_H)
check_include_file(ndir.h HAVE_NDIR_H)
if( NOT PURE_WINDOWS )
  check_include_file(pthread.h HAVE_PTHREAD_H)
endif()
check_include_file(sanitizer/msan_interface.h HAVE_SANITIZER_MSAN_INTERFACE_H)
check_include_file(signal.h HAVE_SIGNAL_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(sys/dir.h HAVE_SYS_DIR_H)
check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
check_include_file(sys/ndir.h HAVE_SYS_NDIR_H)
check_include_file(sys/param.h HAVE_SYS_PARAM_H)
check_include_file(sys/resource.h HAVE_SYS_RESOURCE_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(sys/uio.h HAVE_SYS_UIO_H)
check_include_file(sys/wait.h HAVE_SYS_WAIT_H)
check_include_file(termios.h HAVE_TERMIOS_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(utime.h HAVE_UTIME_H)
check_include_file(valgrind/valgrind.h HAVE_VALGRIND_VALGRIND_H)
check_include_file(zlib.h HAVE_ZLIB_H)
check_include_file(fenv.h HAVE_FENV_H)
check_symbol_exists(FE_ALL_EXCEPT "fenv.h" HAVE_DECL_FE_ALL_EXCEPT)
check_symbol_exists(FE_INEXACT "fenv.h" HAVE_DECL_FE_INEXACT)

check_include_file(mach/mach.h HAVE_MACH_MACH_H)
check_include_file(mach-o/dyld.h HAVE_MACH_O_DYLD_H)

# library checks
if( NOT PURE_WINDOWS )
  check_library_exists(pthread pthread_create "" HAVE_LIBPTHREAD)
  if (HAVE_LIBPTHREAD)
    check_library_exists(pthread pthread_getspecific "" HAVE_PTHREAD_GETSPECIFIC)
    check_library_exists(pthread pthread_rwlock_init "" HAVE_PTHREAD_RWLOCK_INIT)
    check_library_exists(pthread pthread_mutex_lock "" HAVE_PTHREAD_MUTEX_LOCK)
  else()
    # this could be Android
    check_library_exists(c pthread_create "" PTHREAD_IN_LIBC)
    if (PTHREAD_IN_LIBC)
      check_library_exists(c pthread_getspecific "" HAVE_PTHREAD_GETSPECIFIC)
      check_library_exists(c pthread_rwlock_init "" HAVE_PTHREAD_RWLOCK_INIT)
      check_library_exists(c pthread_mutex_lock "" HAVE_PTHREAD_MUTEX_LOCK)
    endif()
  endif()
  check_library_exists(dl dlopen "" HAVE_LIBDL)
  check_library_exists(rt clock_gettime "" HAVE_LIBRT)
  if (LLVM_ENABLE_ZLIB)
    check_library_exists(z compress2 "" HAVE_LIBZ)
  else()
    set(HAVE_LIBZ 0)
  endif()
  if(LLVM_ENABLE_TERMINFO)
    set(HAVE_TERMINFO 0)
    foreach(library tinfo terminfo curses ncurses ncursesw)
      string(TOUPPER ${library} library_suffix)
      check_library_exists(${library} setupterm "" HAVE_TERMINFO_${library_suffix})
      if(HAVE_TERMINFO_${library_suffix})
        set(HAVE_TERMINFO 1)
        set(TERMINFO_LIBS "${library}")
        break()
      endif()
    endforeach()
  else()
    set(HAVE_TERMINFO 0)
  endif()
endif()

# function checks
check_symbol_exists(arc4random "stdlib.h" HAVE_ARC4RANDOM)
check_symbol_exists(backtrace "execinfo.h" HAVE_BACKTRACE)
check_symbol_exists(getpagesize unistd.h HAVE_GETPAGESIZE)
check_symbol_exists(getrusage sys/resource.h HAVE_GETRUSAGE)
check_symbol_exists(setrlimit sys/resource.h HAVE_SETRLIMIT)
check_symbol_exists(isatty unistd.h HAVE_ISATTY)
check_symbol_exists(isinf cmath HAVE_ISINF_IN_CMATH)
check_symbol_exists(isinf math.h HAVE_ISINF_IN_MATH_H)
check_symbol_exists(finite ieeefp.h HAVE_FINITE_IN_IEEEFP_H)
check_symbol_exists(isnan cmath HAVE_ISNAN_IN_CMATH)
check_symbol_exists(isnan math.h HAVE_ISNAN_IN_MATH_H)
check_symbol_exists(ceilf math.h HAVE_CEILF)
check_symbol_exists(floorf math.h HAVE_FLOORF)
check_symbol_exists(fmodf math.h HAVE_FMODF)
check_symbol_exists(log math.h HAVE_LOG)
check_symbol_exists(log2 math.h HAVE_LOG2)
check_symbol_exists(log10 math.h HAVE_LOG10)
check_symbol_exists(exp math.h HAVE_EXP)
check_symbol_exists(exp2 math.h HAVE_EXP2)
check_symbol_exists(exp10 math.h HAVE_EXP10)
check_symbol_exists(futimens sys/stat.h HAVE_FUTIMENS)
check_symbol_exists(futimes sys/time.h HAVE_FUTIMES)
if( HAVE_SETJMP_H )
  check_symbol_exists(longjmp setjmp.h HAVE_LONGJMP)
  check_symbol_exists(setjmp setjmp.h HAVE_SETJMP)
  check_symbol_exists(siglongjmp setjmp.h HAVE_SIGLONGJMP)
  check_symbol_exists(sigsetjmp setjmp.h HAVE_SIGSETJMP)
endif()
if( HAVE_SYS_UIO_H )
  check_symbol_exists(writev sys/uio.h HAVE_WRITEV)
endif()
check_symbol_exists(nearbyintf math.h HAVE_NEARBYINTF)
check_symbol_exists(mallinfo malloc.h HAVE_MALLINFO)
check_symbol_exists(malloc_zone_statistics malloc/malloc.h
                    HAVE_MALLOC_ZONE_STATISTICS)
check_symbol_exists(mkdtemp "stdlib.h;unistd.h" HAVE_MKDTEMP)
check_symbol_exists(mkstemp "stdlib.h;unistd.h" HAVE_MKSTEMP)
check_symbol_exists(mktemp "stdlib.h;unistd.h" HAVE_MKTEMP)
check_symbol_exists(closedir "sys/types.h;dirent.h" HAVE_CLOSEDIR)
check_symbol_exists(opendir "sys/types.h;dirent.h" HAVE_OPENDIR)
check_symbol_exists(readdir "sys/types.h;dirent.h" HAVE_READDIR)
check_symbol_exists(getcwd unistd.h HAVE_GETCWD)
check_symbol_exists(gettimeofday sys/time.h HAVE_GETTIMEOFDAY)
check_symbol_exists(getrlimit "sys/types.h;sys/time.h;sys/resource.h" HAVE_GETRLIMIT)
check_symbol_exists(posix_spawn spawn.h HAVE_POSIX_SPAWN)
check_symbol_exists(pread unistd.h HAVE_PREAD)
check_symbol_exists(realpath stdlib.h HAVE_REALPATH)
check_symbol_exists(sbrk unistd.h HAVE_SBRK)
check_symbol_exists(srand48 stdlib.h HAVE_RAND48_SRAND48)
if( HAVE_RAND48_SRAND48 )
  check_symbol_exists(lrand48 stdlib.h HAVE_RAND48_LRAND48)
  if( HAVE_RAND48_LRAND48 )
    check_symbol_exists(drand48 stdlib.h HAVE_RAND48_DRAND48)
    if( HAVE_RAND48_DRAND48 )
      set(HAVE_RAND48 1 CACHE INTERNAL "are srand48/lrand48/drand48 available?")
    endif()
  endif()
endif()
check_symbol_exists(strtoll stdlib.h HAVE_STRTOLL)
check_symbol_exists(strtoq stdlib.h HAVE_STRTOQ)
check_symbol_exists(strerror string.h HAVE_STRERROR)
check_symbol_exists(strerror_r string.h HAVE_STRERROR_R)
check_symbol_exists(strerror_s string.h HAVE_DECL_STRERROR_S)
check_symbol_exists(setenv stdlib.h HAVE_SETENV)
if( PURE_WINDOWS )
  check_symbol_exists(_chsize_s io.h HAVE__CHSIZE_S)

  check_function_exists(_alloca HAVE__ALLOCA)
  check_function_exists(__alloca HAVE___ALLOCA)
  check_function_exists(__chkstk HAVE___CHKSTK)
  check_function_exists(___chkstk HAVE____CHKSTK)

  check_function_exists(__ashldi3 HAVE___ASHLDI3)
  check_function_exists(__ashrdi3 HAVE___ASHRDI3)
  check_function_exists(__divdi3 HAVE___DIVDI3)
  check_function_exists(__fixdfdi HAVE___FIXDFDI)
  check_function_exists(__fixsfdi HAVE___FIXSFDI)
  check_function_exists(__floatdidf HAVE___FLOATDIDF)
  check_function_exists(__lshrdi3 HAVE___LSHRDI3)
  check_function_exists(__moddi3 HAVE___MODDI3)
  check_function_exists(__udivdi3 HAVE___UDIVDI3)
  check_function_exists(__umoddi3 HAVE___UMODDI3)

  check_function_exists(__main HAVE___MAIN)
  check_function_exists(__cmpdi2 HAVE___CMPDI2)
endif()
if( HAVE_DLFCN_H )
  if( HAVE_LIBDL )
    list(APPEND CMAKE_REQUIRED_LIBRARIES dl)
  endif()
  check_symbol_exists(dlerror dlfcn.h HAVE_DLERROR)
  check_symbol_exists(dlopen dlfcn.h HAVE_DLOPEN)
  if( HAVE_LIBDL )
    list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES dl)
  endif()
endif()

check_symbol_exists(__GLIBC__ stdio.h LLVM_USING_GLIBC)
if( LLVM_USING_GLIBC )
  add_llvm_definitions( -D_GNU_SOURCE )
endif()

set(headers "sys/types.h")

if (HAVE_INTTYPES_H)
  set(headers ${headers} "inttypes.h")
endif()

if (HAVE_STDINT_H)
  set(headers ${headers} "stdint.h")
endif()

check_type_exists(int64_t "${headers}" HAVE_INT64_T)
check_type_exists(uint64_t "${headers}" HAVE_UINT64_T)
check_type_exists(u_int64_t "${headers}" HAVE_U_INT64_T)

# available programs checks
function(llvm_find_program name)
  string(TOUPPER ${name} NAME)
  string(REGEX REPLACE "\\." "_" NAME ${NAME})

  find_program(LLVM_PATH_${NAME} NAMES ${ARGV})
  mark_as_advanced(LLVM_PATH_${NAME})
  if(LLVM_PATH_${NAME})
    set(HAVE_${NAME} 1 CACHE INTERNAL "Is ${name} available ?")
    mark_as_advanced(HAVE_${NAME})
  else(LLVM_PATH_${NAME})
    set(HAVE_${NAME} "" CACHE INTERNAL "Is ${name} available ?")
  endif(LLVM_PATH_${NAME})
endfunction()

llvm_find_program(gv)
llvm_find_program(circo)
llvm_find_program(twopi)
llvm_find_program(neato)
llvm_find_program(fdp)
llvm_find_program(dot)
llvm_find_program(dotty)
llvm_find_program(xdot xdot.py)
llvm_find_program(Graphviz)

if( LLVM_ENABLE_FFI )
  find_path(FFI_INCLUDE_PATH ffi.h PATHS ${FFI_INCLUDE_DIR})
  if( FFI_INCLUDE_PATH )
    set(FFI_HEADER ffi.h CACHE INTERNAL "")
    set(HAVE_FFI_H 1 CACHE INTERNAL "")
  else()
    find_path(FFI_INCLUDE_PATH ffi/ffi.h PATHS ${FFI_INCLUDE_DIR})
    if( FFI_INCLUDE_PATH )
      set(FFI_HEADER ffi/ffi.h CACHE INTERNAL "")
      set(HAVE_FFI_FFI_H 1 CACHE INTERNAL "")
    endif()
  endif()

  if( NOT FFI_HEADER )
    message(FATAL_ERROR "libffi includes are not found.")
  endif()

  find_library(FFI_LIBRARY_PATH ffi PATHS ${FFI_LIBRARY_DIR})
  if( NOT FFI_LIBRARY_PATH )
    message(FATAL_ERROR "libffi is not found.")
  endif()

  list(APPEND CMAKE_REQUIRED_LIBRARIES ${FFI_LIBRARY_PATH})
  list(APPEND CMAKE_REQUIRED_INCLUDES ${FFI_INCLUDE_PATH})
  check_symbol_exists(ffi_call ${FFI_HEADER} HAVE_FFI_CALL)
  list(REMOVE_ITEM CMAKE_REQUIRED_INCLUDES ${FFI_INCLUDE_PATH})
  list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES ${FFI_LIBRARY_PATH})
else()
  unset(HAVE_FFI_FFI_H CACHE)
  unset(HAVE_FFI_H CACHE)
  unset(HAVE_FFI_CALL CACHE)
endif( LLVM_ENABLE_FFI )

# Define LLVM_HAS_ATOMICS if gcc or MSVC atomic builtins are supported.
include(CheckAtomic)

if( LLVM_ENABLE_PIC )
  set(ENABLE_PIC 1)
else()
  set(ENABLE_PIC 0)
endif()

find_package(LibXml2)
if (LIBXML2_FOUND)
  set(CLANG_HAVE_LIBXML 1)
  # When cross-compiling, liblzma is not detected as a dependency for libxml2,
  # which makes linking c-index-test fail. But for native builds, all libraries
  # are installed and checked by CMake before Makefiles are generated and everything
  # works according to the plan. However, if a -llzma is added to native builds,
  # an additional requirement on the static liblzma.a is required, but will not
  # be checked by CMake, breaking native compilation.
  # Since this is only pertinent to cross-compilations, and there's no way CMake
  # can check for every foreign library on every OS, we add the dep and warn the dev.
  if ( CMAKE_CROSSCOMPILING )
    if (NOT PC_LIBXML_VERSION VERSION_LESS "2.8.0")
      message(STATUS "Adding LZMA as a dep to XML2 for cross-compilation, make sure liblzma.a is available.")
      set(LIBXML2_LIBRARIES ${LIBXML2_LIBRARIES} "-llzma")
    endif ()
  endif ()
endif ()

option(LLVM_FORCE_USE_OLD_TOOLCHAIN
       "Set to ON if you want to force CMake to use a toolchain older than those supported by LLVM."
       OFF)
if(NOT LLVM_FORCE_USE_OLD_TOOLCHAIN)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.7)
      message(FATAL_ERROR "Host GCC version must be at least 4.7!")
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.1)
      message(FATAL_ERROR "Host Clang version must be at least 3.1!")
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17.0)
      message(FATAL_ERROR "Host Visual Studio must be at least 2012 (MSVC 17.0)")
    endif()
  endif()
endif()

include(CheckCXXCompilerFlag)

check_cxx_compiler_flag("-Wno-variadic-macros" SUPPORTS_NO_VARIADIC_MACROS_FLAG)

set(USE_NO_MAYBE_UNINITIALIZED 0)
set(USE_NO_UNINITIALIZED 0)

# Disable gcc's potentially uninitialized use analysis as it presents lots of
# false positives.
if (CMAKE_COMPILER_IS_GNUCXX)
  check_cxx_compiler_flag("-Wmaybe-uninitialized" HAS_MAYBE_UNINITIALIZED)
  if (HAS_MAYBE_UNINITIALIZED)
    set(USE_NO_MAYBE_UNINITIALIZED 1)
  else()
    # Only recent versions of gcc make the distinction between -Wuninitialized
    # and -Wmaybe-uninitialized. If -Wmaybe-uninitialized isn't supported, just
    # turn off all uninitialized use warnings.
    check_cxx_compiler_flag("-Wuninitialized" HAS_UNINITIALIZED)
    set(USE_NO_UNINITIALIZED ${HAS_UNINITIALIZED})
  endif()
endif()

# By default, we target the host, but this can be overridden at CMake
# invocation time.
include(GetHostTriple)
get_host_triple(LLVM_INFERRED_HOST_TRIPLE)

set(LLVM_HOST_TRIPLE "${LLVM_INFERRED_HOST_TRIPLE}" CACHE STRING
    "Host on which LLVM binaries will run")

# Determine the native architecture.
string(TOLOWER "${LLVM_TARGET_ARCH}" LLVM_NATIVE_ARCH)
if( LLVM_NATIVE_ARCH STREQUAL "host" )
  string(REGEX MATCH "^[^-]*" LLVM_NATIVE_ARCH ${LLVM_HOST_TRIPLE})
endif ()

if (LLVM_NATIVE_ARCH MATCHES "i[2-6]86")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "x86")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "amd64")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "x86_64")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH MATCHES "sparc")
  set(LLVM_NATIVE_ARCH Sparc)
elseif (LLVM_NATIVE_ARCH MATCHES "powerpc")
  set(LLVM_NATIVE_ARCH PowerPC)
elseif (LLVM_NATIVE_ARCH MATCHES "aarch64")
  set(LLVM_NATIVE_ARCH AArch64)
elseif (LLVM_NATIVE_ARCH MATCHES "arm")
  set(LLVM_NATIVE_ARCH ARM)
elseif (LLVM_NATIVE_ARCH MATCHES "mips")
  set(LLVM_NATIVE_ARCH Mips)
elseif (LLVM_NATIVE_ARCH MATCHES "xcore")
  set(LLVM_NATIVE_ARCH XCore)
elseif (LLVM_NATIVE_ARCH MATCHES "msp430")
  set(LLVM_NATIVE_ARCH MSP430)
elseif (LLVM_NATIVE_ARCH MATCHES "hexagon")
  set(LLVM_NATIVE_ARCH Hexagon)
elseif (LLVM_NATIVE_ARCH MATCHES "s390x")
  set(LLVM_NATIVE_ARCH SystemZ)
else ()
  message(FATAL_ERROR "Unknown architecture ${LLVM_NATIVE_ARCH}")
endif ()

# If build targets includes "host", then replace with native architecture.
list(FIND LLVM_TARGETS_TO_BUILD "host" idx)
if( NOT idx LESS 0 )
  list(REMOVE_AT LLVM_TARGETS_TO_BUILD ${idx})
  list(APPEND LLVM_TARGETS_TO_BUILD ${LLVM_NATIVE_ARCH})
  list(REMOVE_DUPLICATES LLVM_TARGETS_TO_BUILD)
endif()

list(FIND LLVM_TARGETS_TO_BUILD ${LLVM_NATIVE_ARCH} NATIVE_ARCH_IDX)
if (NATIVE_ARCH_IDX EQUAL -1)
  message(STATUS
    "Native target ${LLVM_NATIVE_ARCH} is not selected; lli will not JIT code")
else ()
  message(STATUS "Native target architecture is ${LLVM_NATIVE_ARCH}")
  set(LLVM_NATIVE_TARGET LLVMInitialize${LLVM_NATIVE_ARCH}Target)
  set(LLVM_NATIVE_TARGETINFO LLVMInitialize${LLVM_NATIVE_ARCH}TargetInfo)
  set(LLVM_NATIVE_TARGETMC LLVMInitialize${LLVM_NATIVE_ARCH}TargetMC)
  set(LLVM_NATIVE_ASMPRINTER LLVMInitialize${LLVM_NATIVE_ARCH}AsmPrinter)

  # We don't have an ASM parser for all architectures yet.
  if (EXISTS ${CMAKE_SOURCE_DIR}/lib/Target/${LLVM_NATIVE_ARCH}/AsmParser/CMakeLists.txt)
    set(LLVM_NATIVE_ASMPARSER LLVMInitialize${LLVM_NATIVE_ARCH}AsmParser)
  endif ()

  # We don't have an disassembler for all architectures yet.
  if (EXISTS ${CMAKE_SOURCE_DIR}/lib/Target/${LLVM_NATIVE_ARCH}/Disassembler/CMakeLists.txt)
    set(LLVM_NATIVE_DISASSEMBLER LLVMInitialize${LLVM_NATIVE_ARCH}Disassembler)
  endif ()
endif ()

if( MINGW )
  set(HAVE_LIBIMAGEHLP 1)
  set(HAVE_LIBPSAPI 1)
  set(HAVE_LIBSHELL32 1)
  # TODO: Check existence of libraries.
  #   include(CheckLibraryExists)
  #   CHECK_LIBRARY_EXISTS(imagehlp ??? . HAVE_LIBIMAGEHLP)
endif( MINGW )

if (NOT HAVE_STRTOLL)
  # Use _strtoi64 if strtoll is not available.
  check_symbol_exists(_strtoi64 stdlib.h have_strtoi64)
  if (have_strtoi64)
    set(HAVE_STRTOLL 1)
    set(strtoll "_strtoi64")
    set(strtoull "_strtoui64")
  endif ()
endif ()

if( MSVC )
  set(SHLIBEXT ".lib")
  set(stricmp "_stricmp")
  set(strdup "_strdup")
endif( MSVC )

if( PURE_WINDOWS )
  CHECK_CXX_SOURCE_COMPILES("
    #include <windows.h>
    #include <imagehlp.h>
    extern \"C\" void foo(PENUMLOADED_MODULES_CALLBACK);
    extern \"C\" void foo(BOOL(CALLBACK*)(PCSTR,ULONG_PTR,ULONG,PVOID));
    int main(){return 0;}"
    HAVE_ELMCB_PCSTR)
  if( HAVE_ELMCB_PCSTR )
    set(WIN32_ELMCB_PCSTR "PCSTR")
  else()
    set(WIN32_ELMCB_PCSTR "PSTR")
  endif()
endif( PURE_WINDOWS )

# FIXME: Signal handler return type, currently hardcoded to 'void'
set(RETSIGTYPE void)

if( LLVM_ENABLE_THREADS )
  # Check if threading primitives aren't supported on this platform
  if( NOT HAVE_PTHREAD_H AND NOT WIN32 )
    set(LLVM_ENABLE_THREADS 0)
  endif()
endif()

if( LLVM_ENABLE_THREADS )
  message(STATUS "Threads enabled.")
else( LLVM_ENABLE_THREADS )
  message(STATUS "Threads disabled.")
endif()

if (LLVM_ENABLE_ZLIB )
  # Check if zlib is available in the system.
  if ( NOT HAVE_ZLIB_H OR NOT HAVE_LIBZ )
    set(LLVM_ENABLE_ZLIB 0)
  endif()
endif()

set(LLVM_PREFIX ${CMAKE_INSTALL_PREFIX})

if (LLVM_ENABLE_DOXYGEN)
  message(STATUS "Doxygen enabled.")
  find_package(Doxygen)

  if (DOXYGEN_FOUND)
    # If we find doxygen and we want to enable doxygen by default create a
    # global aggregate doxygen target for generating llvm and any/all
    # subprojects doxygen documentation.
    if (LLVM_BUILD_DOCS)
      add_custom_target(doxygen ALL)
    endif()

    option(LLVM_DOXYGEN_EXTERNAL_SEARCH "Enable doxygen external search." OFF)
    if (LLVM_DOXYGEN_EXTERNAL_SEARCH)
      set(LLVM_DOXYGEN_SEARCHENGINE_URL "" CACHE STRING "URL to use for external searhc.")
      set(LLVM_DOXYGEN_SEARCH_MAPPINGS "" CACHE STRING "Doxygen Search Mappings")
    endif()
  endif()
else()
  message(STATUS "Doxygen disabled.")
endif()
