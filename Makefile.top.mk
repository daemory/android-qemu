LOCAL_PATH:= $(call my-dir)

# This defines EMULATOR_BUILD_32BITS to indicate that 32-bit binaries
# must be generated by the build system. For now, only for Windows because
# the Win64 do not work yet properly (e.g. can't emulate 32-bit ARM), and
# Linux (because the 32-bit binaries are deprecated, but not obsolete).
EMULATOR_BUILD_32BITS := $(strip $(filter windows linux,$(HOST_OS)))

# This defines EMULATOR_BUILD_64BITS to indicate that 64-bit binaries
# must be generated by the build system. For now, only do it for
# Windows, Linux and Darwin.
EMULATOR_BUILD_64BITS := $(strip $(filter linux darwin windows,$(HOST_OS)))

# EMULATOR_PROGRAM_BITNESS is the bitness of the 'emulator' launcher program.
# It will be 32 if we allow 32-bit binaries to be built, 64 otherwise.
ifneq (,$(EMULATOR_BUILD_32BITS))
    EMULATOR_PROGRAM_BITNESS := 32
else
    EMULATOR_PROGRAM_BITNESS := 64
endif

# A function that includes a file only if 32-bit binaries are necessary,
# or if LOCAL_IGNORE_BITNESS is defined for the current module.
# $1: Build file to include.
include-if-bitness-32 = \
    $(if $(strip $(LOCAL_IGNORE_BITNESS)$(filter true,$(LOCAL_HOST_BUILD))$(EMULATOR_BUILD_32BITS)),\
        $(eval include $1))

# A function that includes a file only of EMULATOR_BUILD_64BITS is not empty.
# or if LOCAL_IGNORE_BITNESS is defined for the current module.
# $1: Build file to include.
include-if-bitness-64 = \
    $(if $(strip $(LOCAL_IGNORE_BITNESS)$(filter true,$(LOCAL_HOST_BUILD))$(EMULATOR_BUILD_64BITS)),\
        $(eval include $1))

MY_CC  := $(HOST_CC)
MY_CXX := $(HOST_CXX)
MY_LD  := $(HOST_LD)
MY_AR  := $(HOST_AR)
MY_WINDRES := $(HOST_WINDRES)
MY_DUMPSYMS  := $(HOST_DUMPSYMS)

MY_CFLAGS := -g -falign-functions=0
ifeq ($(BUILD_DEBUG_EMULATOR),true)
    MY_CFLAGS += -O0
else
    MY_CFLAGS += -O2
endif

# Generate position-independent binaries. Don't add -fPIC when targetting
# Windows, because newer toolchain complain loudly about it, since all
# Windows code is position-independent.
ifneq (windows,$(HOST_OS))
  MY_CFLAGS += -fPIC
endif

# Ensure that <inttypes.h> always defines all interesting macros.
MY_CFLAGS += -D__STDC_LIMIT_MACROS=1 -D__STDC_FORMAT_MACROS=1

MY_CFLAGS32 :=
MY_CFLAGS64 :=

MY_LDLIBS :=
MY_LDLIBS32 :=
MY_LDLIBS64 :=

MY_LDFLAGS :=
MY_LDFLAGS32 :=
MY_LDFLAGS64 :=

ifeq ($(HOST_OS),freebsd)
  MY_CFLAGS += -I /usr/local/include
endif

ifeq ($(HOST_OS),windows)
  # we need Win32 features that are available since Windows 2000 Professional/Server (NT 5.0)
  MY_CFLAGS += -DWINVER=0x501
  # LARGEADDRESSAWARE gives more address space to 32-bit process
  MY_LDFLAGS32 += -Xlinker --large-address-aware
endif

ifeq ($(HOST_OS),darwin)
    MY_CFLAGS += -D_DARWIN_C_SOURCE=1
    # Clang complains about this flag being not useful anymore.
    MY_CFLAGS := $(filter-out -falign-functions=0,$(MY_CFLAGS))
endif

# NOTE: The following definitions are only used by the standalone build.
MY_EXEEXT :=
MY_DLLEXT := .so
ifeq ($(HOST_OS),windows)
  MY_EXEEXT := .exe
  MY_DLLEXT := .dll
endif
ifeq ($(HOST_OS),darwin)
  MY_DLLEXT := .dylib
endif

# Some CFLAGS below use -Wno-missing-field-initializers but this is not
# supported on GCC 3.x which is still present under Cygwin.
# Find out by probing GCC for support of this flag. Note that the test
# itself only works on GCC 4.x anyway.
GCC_W_NO_MISSING_FIELD_INITIALIZERS := -Wno-missing-field-initializers
ifeq ($(HOST_OS),windows)
    ifeq (,$(shell gcc -Q --help=warnings 2>/dev/null | grep missing-field-initializers))
        $(info emulator: Ignoring unsupported GCC flag $(GCC_W_NO_MISSING_FIELD_INITIALIZERS))
        GCC_W_NO_MISSING_FIELD_INITIALIZERS :=
    endif
endif

ifeq ($(HOST_OS),windows)
  # Ensure that printf() et al use GNU printf format specifiers as required
  # by QEMU. This is important when using the newer Mingw64 cross-toolchain.
  # See http://sourceforge.net/apps/trac/mingw-w64/wiki/gnu%20printf
  MY_CFLAGS += -D__USE_MINGW_ANSI_STDIO=1
endif

# Enable warning, except those related to missing field initializers
# (the QEMU coding style loves using these).
#
MY_CFLAGS += -Wall $(GCC_W_NO_MISSING_FIELD_INITIALIZERS)

# Needed to build block.c on Linux/x86_64.
MY_CFLAGS += -D_GNU_SOURCE=1

# A useful function that can be used to start the declaration of a host
# module. Avoids repeating the same stuff again and again.
# Usage:
#
#  $(call start-emulator-library, <module-name>)
#
#  ... declarations
#
#  $(call end-emulator-library)
#
start-emulator-library = \
    $(eval include $(CLEAR_VARS)) \
    $(eval LOCAL_NO_DEFAULT_COMPILER_FLAGS := true) \
    $(eval LOCAL_MODULE := $1) \
    $(eval LOCAL_MODULE_CLASS := STATIC_LIBRARIES) \
    $(eval LOCAL_BUILD_FILE := $(BUILD_HOST_STATIC_LIBRARY))

# Used with start-emulator-library
end-emulator-library = \
    $(eval $(end-emulator-module-ev)) \

define-emulator-prebuilt-library = \
    $(call start-emulator-library,$1) \
    $(eval LOCAL_BUILD_FILE := $(PREBUILT_STATIC_LIBRARY)) \
    $(eval LOCAL_SRC_FILES := $2) \
    $(eval $(end-emulator-module-ev)) \

# A variant of start-emulator-library to start the definition of a host
# program instead. Use with end-emulator-program
start-emulator-program = \
    $(call start-emulator-library,$1) \
    $(eval LOCAL_MODULE_CLASS := EXECUTABLES) \
    $(eval LOCAL_BUILD_FILE := $(BUILD_HOST_EXECUTABLE))

# A varient of end-emulator-library for host programs instead
end-emulator-program = \
    $(eval LOCAL_LDLIBS += $(QEMU_SYSTEM_LDLIBS)) \
    $(eval $(end-emulator-module-ev)) \

define end-emulator-module-ev
$(call local-host-define,CC)
$(call local-host-define,CXX)
$(call local-host-define,AR)
$(call local-host-define,LD)
$(call local-host-define,SYMTOOL)

LOCAL_CFLAGS := \
    $$(call local-host-tool,CFLAGS$$(HOST_BITS)) \
    $$(call local-host-tool,CFLAGS) \
    $$(LOCAL_CFLAGS)

LOCAL_LDFLAGS := \
    $$(call local-host-tool,LDFLAGS$$(HOST_BITS)) \
    $$(call local-host-tool,LDFLAGS) \
    $$(LOCAL_LDFLAGS)

LOCAL_LDLIBS := \
    $$(LOCAL_LDLIBS) \
    $$(call local-host-tool,LDLIBS) \
    $$(call local-host-tool,LDLIBS$$(HOST_BITS))

# Ensure only one of -m32 or -m64 is being used and place it first.
LOCAL_CFLAGS := \
    -m$$(HOST_BITS) \
    $$(filter-out -m32 -m64, $$(LOCAL_CFLAGS))

LOCAL_LDFLAGS := \
    -m$$(HOST_BITS) \
    $$(filter-out -m32 -m64, $$(LOCAL_LDFLAGS))

include $$(LOCAL_BUILD_FILE)
endef

# The common libraries
#
QEMU_SYSTEM_LDLIBS := -lm
ifeq ($(HOST_OS),windows)
  QEMU_SYSTEM_LDLIBS += -mwindows -mconsole
endif

ifeq ($(HOST_OS),freebsd)
    QEMU_SYSTEM_LDLIBS += -L/usr/local/lib -lpthread -lX11 -lutil
endif

ifeq ($(HOST_OS),linux)
  QEMU_SYSTEM_LDLIBS += -lutil -lrt
endif

ifeq ($(HOST_OS),windows)
  # amd64-mingw32msvc- toolchain still name it ws2_32.  May change it once amd64-mingw32msvc-
  # is stabilized
  QEMU_SYSTEM_LDLIBS += -lwinmm -lws2_32 -liphlpapi
else
  QEMU_SYSTEM_LDLIBS += -lpthread
endif

ifeq ($(HOST_OS),darwin)
  QEMU_SYSTEM_FRAMEWORKS := \
      AudioUnit \
      AVFoundation \
      Cocoa \
      CoreAudio \
      CoreMedia \
      CoreVideo \
      ForceFeedback \
      QTKit \

  QEMU_SYSTEM_LDLIBS += $(QEMU_SYSTEM_FRAMEWORKS:%=-Wl,-framework,%)
endif

ifeq ($(HOST_OS),darwin)
    CXX_STD_LIB := -lc++
else
    CXX_STD_LIB := -lstdc++
endif

# Call this function to force a module to link statically to the C++ standard
# library on platforms that support it (i.e. Linux and Windows).
local-link-static-c++lib = $(eval $(ev-local-link-static-c++lib))
define ev-local-link-static-c++lib
ifeq (darwin,$(HOST_OS))
LOCAL_LDLIBS += $(CXX_STD_LIB)
else  # HOST_OS != darwin
LOCAL_LD := $$(call local-host-tool,CXX)
LOCAL_LDLIBS += -static-libstdc++
endif  # HOST_OS != darwin
endef

ifdef EMULATOR_BUILD_32BITS
HOST_BITS := 32
HOST_ARCH := x86
HOST_SUFFIX :=
include $(LOCAL_PATH)/Makefile.common.mk
endif

ifdef EMULATOR_BUILD_64BITS
HOST_BITS := 64
HOST_ARCH := x86_64
HOST_SUFFIX := 64

include $(LOCAL_PATH)/Makefile.common.mk
endif

## VOILA!!
