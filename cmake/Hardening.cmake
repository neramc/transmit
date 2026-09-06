# The mitigations a program that reads other people's files should be built
# with, asked for rather than inherited.
#
# Every one of these is already the default on Debian and Ubuntu, which is why
# the binaries have them today. That is the distribution's decision, not this
# project's: the same source built with a plain gcc, or on a distribution that
# has not made those defaults, gets none of them and nothing says so. Asking
# explicitly makes the guarantee belong to the build, and scripts/check-
# hardening.sh holds it to it.

if(MSVC)
    # /GS   stack cookies, /guard:cf control flow guard.
    # /DYNAMICBASE and /NXCOMPAT are the loader's half and are linker options.
    add_compile_options(/GS /guard:cf)
    add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
    return()
endif()

include(CheckCXXCompilerFlag)

# Undefined first: Ubuntu's compiler defines it already, and redefining a macro
# is a warning, which this build turns into an error.
add_compile_options(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
add_compile_options(-fstack-protector-strong)

check_cxx_compiler_flag(-fstack-clash-protection TRANSMIT_HAS_STACK_CLASH)
if(TRANSMIT_HAS_STACK_CLASH)
    add_compile_options(-fstack-clash-protection)
endif()

# Indirect branch tracking and shadow stack markers. Free where the hardware
# has them and ignored where it does not, but a binary without the marker can
# never be protected by either.
check_cxx_compiler_flag(-fcf-protection=full TRANSMIT_HAS_CF_PROTECTION)
if(TRANSMIT_HAS_CF_PROTECTION)
    add_compile_options(-fcf-protection=full)
endif()

# include() does not open a scope, so this lands in the top-level directory
# where the targets are defined.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT APPLE)
    # Full RELRO: the relocation table is resolved at load and made read-only,
    # so an overwritten function pointer there is not a way to redirect a call.
    add_link_options(-Wl,-z,relro -Wl,-z,now)
    # A stack that is never executable, said in the binary rather than left to
    # the loader's guess.
    add_link_options(-Wl,-z,noexecstack)
endif()
