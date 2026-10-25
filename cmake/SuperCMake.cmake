# ============================================================================
# SuperCMake.cmake
#
# A CMake re-implementation of kingbirdogd/super_make's `common.mk`.
#
# It provides the same convention-over-configuration build model: every module
# lives in its own directory, declares a handful of variables, and calls a
# single command.  The original Makefile idiom
#
#       TYPE:=SHARE
#       DEPS:=static_example
#       include $(PROJECT_HOME)/common.mk
#
# becomes
#
#       set(TYPE SHARE)
#       set(DEPS static_example)
#       super_module()
#
# Supported per-module knobs (all optional, read from the calling scope):
#   TYPE          EXE | STATIC | SHARE | REF | NONE      (default: NONE)
#   DEPS          space/;-list of sibling module names   (internal deps)
#   DEP_PKGS      space/;-list of pkg-config packages
#   VERSION       x.y.z                                   (default: 0.0.1)
#   STD           c++NN / gnu++NN / NN                    (default: c++17)
#   LIB_PREFIX    library name prefix                     (default: lib)
#   SHARE_SUFFIX  shared-library suffix (a.k.a. SHARE_SUBFFIX, default .so)
#   C_FLAGS / CPP_FLAGS / EXE_FLAGS / SHARE_FLAGS         extra compile/link flags
#   INC_PATHS / LIB_PATHS / LIBS                          extra include/link paths & libs
#
# Module directory layout (identical to super_make):
#   <module>/src/*.c , *.cpp     translation units
#   <module>/inc/                public headers (exported to dependents)
#
# TYPE meanings:
#   STATIC  ->  static archive          (lib<name>.a)
#   SHARE   ->  versioned shared object  (lib<name>.so.<ver> + soname chain)
#   EXE     ->  executable
#   REF     ->  header-only interface    (exposes inc/, compiles nothing)
#   NONE    ->  aggregator: add_subdirectory() every child holding a CMakeLists
#
# Build output.  Like super_make (which kept ../<proj>_build/<proj>_release and
# ../<proj>_build/<proj>_debug side by side and symlinked ../<proj>_build/<proj>
# to the active one), every configuration gets its own subtree and the
# convenience paths are symlinks pointing at the active configuration:
#
#   ${CMAKE_BINARY_DIR}/release/{bin,lib64,symbol}   Release artifacts
#   ${CMAKE_BINARY_DIR}/debug/{bin,lib64,symbol}     Debug artifacts
#   ${CMAKE_BINARY_DIR}/bin    -> <config>/bin        (symlink to active config)
#   ${CMAKE_BINARY_DIR}/lib64  -> <config>/lib64       (symlink to active config)
#   ${CMAKE_BINARY_DIR}/symbol -> <config>/symbol      (symlink to active config)
# ============================================================================

# --- The shared warning / codegen flags from common.mk's BASE_COMPILE_FLAG ---
#   -c -fPIC -std=... are handled natively by CMake (compile step / PIC
#   property / CXX_STANDARD), so only the diagnostic and -g flags remain here.
#   -O3 is added automatically by CMake's Release configuration.
set(SUPER_COMPILE_FLAGS
    -Werror
    -Wfatal-errors
    -Wformat=2
    -Winit-self
    -Wswitch-default
    -Wall
    -Wextra
    -g
    CACHE INTERNAL "super_make BASE_COMPILE_FLAG diagnostics")

# ----------------------------------------------------------------------------
# super_init()
#   Call once from the top-level CMakeLists.  Establishes the default build
#   configuration and the bin/ lib64/ output tree.  Implemented as a macro so
#   the variables land in the calling (top-level) scope and propagate to every
#   add_subdirectory() below it.
# ----------------------------------------------------------------------------
macro(super_init)
    # super_make defaults CONFIG to "release".
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()

    # Every target is built position-independent (common.mk always passes -fPIC),
    # so static libraries can be linked into shared objects.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    if(CMAKE_CONFIGURATION_TYPES)
        # Multi-config generators (e.g. Ninja Multi-Config): pick the subtree at
        # build time via a generator expression; the plain symlinks below are
        # single-config only and are skipped here.
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>/lib64")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>/lib64")
        set(SUPER_SYMBOL_DIR "${CMAKE_BINARY_DIR}/$<LOWER_CASE:$<CONFIG>>/symbol")
    else()
        # Single-config generator: build/<config>/{bin,lib64,symbol}.
        string(TOLOWER "${CMAKE_BUILD_TYPE}" _super_cfg)
        if(NOT _super_cfg)
            set(_super_cfg "release")
        endif()
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_super_cfg}/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_super_cfg}/lib64")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_super_cfg}/lib64")
        set(SUPER_SYMBOL_DIR "${CMAKE_BINARY_DIR}/${_super_cfg}/symbol")

        # Materialise the config subtree so the symlinks below resolve even
        # before the first build populates them.
        file(MAKE_DIRECTORY
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
            "${SUPER_SYMBOL_DIR}")

        # Refresh build/{bin,lib64,symbol} -> build/<config>/* (relative links,
        # mirroring super_make's `ln -sf $(PROJECT_TARGET_PATH) $(PROJECT_BUILD)`).
        foreach(_super_link bin lib64 symbol)
            file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/${_super_link}")
            file(CREATE_LINK "${_super_cfg}/${_super_link}"
                 "${CMAKE_BINARY_DIR}/${_super_link}" SYMBOLIC)
        endforeach()
    endif()
endmacro()

# ----------------------------------------------------------------------------
# _super_add_subdirs()  (internal)
#   The NONE / aggregator behaviour: recurse into every immediate subdirectory
#   that carries a CMakeLists.txt, mirroring common.mk's SUB_PATH discovery.
# ----------------------------------------------------------------------------
function(_super_add_subdirs)
    file(GLOB _entries RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_SOURCE_DIR}/*")
    list(SORT _entries)
    foreach(_e ${_entries})
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_e}"
           AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_e}/CMakeLists.txt")
            add_subdirectory("${_e}")
        endif()
    endforeach()
endfunction()

# ----------------------------------------------------------------------------
# _super_split_debug(<target>)  (internal)
#   common.mk's TRIM_SYMBO: in release builds keep the debug info in a
#   symbol/<file>.sym sidecar, then strip the shipped artifact.
# ----------------------------------------------------------------------------
function(_super_split_debug _tgt)
    if(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        return()
    endif()
    if(NOT CMAKE_OBJCOPY OR NOT CMAKE_STRIP)
        return()
    endif()
    if(DEFINED SUPER_SYMBOL_DIR)
        set(_symdir "${SUPER_SYMBOL_DIR}")
    else()
        set(_symdir "${CMAKE_BINARY_DIR}/symbol")
    endif()
    add_custom_command(TARGET ${_tgt} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_symdir}"
        COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug
                "$<TARGET_FILE:${_tgt}>"
                "${_symdir}/$<TARGET_FILE_NAME:${_tgt}>.sym"
        COMMAND "${CMAKE_STRIP}" --strip-debug --strip-unneeded
                "$<TARGET_FILE:${_tgt}>"
        COMMENT "Splitting debug symbols: ${_tgt} -> symbol/$<TARGET_FILE_NAME:${_tgt}>.sym"
        VERBATIM)
endfunction()

# ----------------------------------------------------------------------------
# _super_apply_common(<target>)  (internal)
#   Include dirs, language standard, diagnostics and the TARGET_NAME / PYINIT
#   preprocessor macros shared by every compiled module.
# ----------------------------------------------------------------------------
function(_super_apply_common _tgt _name)
    # Public include dir -> dependents inherit it (common.mk's -I<dep>/inc).
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/inc")
        target_include_directories(${_tgt} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
    endif()

    # Extra include paths / libs the module opted into.
    if(DEFINED INC_PATHS)
        target_include_directories(${_tgt} PRIVATE ${INC_PATHS})
    endif()

    # Language standard (STD, default c++17).
    if(NOT DEFINED STD OR STD STREQUAL "")
        set(STD "c++17")
    endif()
    string(REGEX REPLACE "^(c|gnu)\\+\\+" "" _stdnum "${STD}")
    set_target_properties(${_tgt} PROPERTIES
        CXX_STANDARD ${_stdnum}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

    # Diagnostics + any user C_FLAGS / CPP_FLAGS.
    target_compile_options(${_tgt} PRIVATE
        ${SUPER_COMPILE_FLAGS}
        $<$<COMPILE_LANGUAGE:C>:${C_FLAGS}>
        $<$<COMPILE_LANGUAGE:CXX>:${CPP_FLAGS}>)

    # MACROS from common.mk.
    target_compile_definitions(${_tgt} PRIVATE
        TARGET_NAME=${_name}
        PYINIT=PyInit_${_name})

    # Internal module dependencies: PUBLIC link => CMake propagates the
    # dependents' include dirs, link dirs and link order automatically,
    # replacing the whole get_deps / DEP_PATH shell machinery.
    if(DEFINED DEPS)
        foreach(_d ${DEPS})
            target_link_libraries(${_tgt} PUBLIC ${_d})
        endforeach()
    endif()

    # System packages (DEP_PKGS) via pkg-config.
    if(DEFINED DEP_PKGS AND NOT DEP_PKGS STREQUAL "")
        find_package(PkgConfig REQUIRED)
        foreach(_pkg ${DEP_PKGS})
            string(MAKE_C_IDENTIFIER "${_pkg}" _pkgid)
            pkg_check_modules(SUPERPKG_${_pkgid} REQUIRED IMPORTED_TARGET ${_pkg})
            target_link_libraries(${_tgt} PUBLIC PkgConfig::SUPERPKG_${_pkgid})
        endforeach()
    endif()

    # Extra link paths / libraries.
    if(DEFINED LIB_PATHS)
        target_link_directories(${_tgt} PRIVATE ${LIB_PATHS})
    endif()
    if(DEFINED LIBS)
        target_link_libraries(${_tgt} PRIVATE ${LIBS})
    endif()
endfunction()

# ----------------------------------------------------------------------------
# _super_apply_naming(<target>)  (internal)
#   LIB_PREFIX / SHARE_SUFFIX overrides (SHARE_SUBFFIX accepted as an alias to
#   match common.mk's original, typo'd variable name).
# ----------------------------------------------------------------------------
function(_super_apply_naming _tgt)
    if(DEFINED LIB_PREFIX)
        # `set(LIB_PREFIX "")` (mirrors common.mk's LIB_PREFIX:="") clears it.
        string(REGEX REPLACE "^\"(.*)\"$" "\\1" _prefix "${LIB_PREFIX}")
        set_target_properties(${_tgt} PROPERTIES PREFIX "${_prefix}")
    endif()
    if(NOT DEFINED SHARE_SUFFIX AND DEFINED SHARE_SUBFFIX)
        set(SHARE_SUFFIX "${SHARE_SUBFFIX}")
    endif()
    if(DEFINED SHARE_SUFFIX AND NOT SHARE_SUFFIX STREQUAL "")
        set_target_properties(${_tgt} PROPERTIES SUFFIX "${SHARE_SUFFIX}")
    endif()
endfunction()

# ----------------------------------------------------------------------------
# _super_version_links(<target> <stem> <version> <major>)  (internal)
#   Build the versioned symlink chain for static libraries and executables,
#   matching common.mk's TARGET_LINK_NAME rules and mirroring the shared-library
#   soname chain CMake produces natively:
#
#       <stem>            -> <stem>.<major>          (symlink)
#       <stem>.<major>    -> <stem>.<version>        (symlink)
#       <stem>.<version>                             (real file)
#
#   The real artifact is produced under the versioned name (the target's SUFFIX
#   already carries `.<version>`); here we only add the two relative symlinks,
#   created in the target's own output directory just like CMake's .so links.
# ----------------------------------------------------------------------------
function(_super_version_links _tgt _stem _version _major)
    if(_version STREQUAL _major)
        # Single-component version: link base name straight to the real file.
        add_custom_command(TARGET ${_tgt} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${_stem}"
            COMMAND "${CMAKE_COMMAND}" -E create_symlink
                    "${_stem}.${_version}" "${_stem}"
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_tgt}>"
            COMMENT "Versioning ${_tgt}: ${_stem} -> ${_stem}.${_version}"
            VERBATIM)
        return()
    endif()
    add_custom_command(TARGET ${_tgt} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${_stem}.${_major}" "${_stem}"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "${_stem}.${_version}" "${_stem}.${_major}"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "${_stem}.${_major}" "${_stem}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${_tgt}>"
        COMMENT "Versioning ${_tgt}: ${_stem} -> ${_stem}.${_major} -> ${_stem}.${_version}"
        VERBATIM)
endfunction()

# ----------------------------------------------------------------------------
# super_module()
#   The public entry point — the CMake equivalent of `include common.mk`.
# ----------------------------------------------------------------------------
function(super_module)
    if(NOT DEFINED TYPE OR TYPE STREQUAL "")
        set(TYPE NONE)
    endif()

    get_filename_component(_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

    # ---- NONE: pure aggregator -------------------------------------------
    if(TYPE STREQUAL "NONE")
        _super_add_subdirs()
        return()
    endif()

    # ---- REF: header-only interface library ------------------------------
    if(TYPE STREQUAL "REF")
        add_library(${_name} INTERFACE)
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/inc")
            target_include_directories(${_name} INTERFACE
                "${CMAKE_CURRENT_SOURCE_DIR}/inc")
        endif()
        if(DEFINED DEPS)
            foreach(_d ${DEPS})
                target_link_libraries(${_name} INTERFACE ${_d})
            endforeach()
        endif()
        return()
    endif()

    # ---- Compiled targets: collect sources -------------------------------
    file(GLOB _srcs CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
         "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

    # VERSION (default 0.0.1) and its major component for the soname.
    if(NOT DEFINED VERSION OR VERSION STREQUAL "")
        set(VERSION "0.0.1")
    endif()
    string(REPLACE "." ";" _vparts "${VERSION}")
    list(GET _vparts 0 _vmajor)

    if(TYPE STREQUAL "STATIC")
        add_library(${_name} STATIC ${_srcs})
    elseif(TYPE STREQUAL "SHARE")
        add_library(${_name} SHARED ${_srcs})
        # Versioned .so + soname chain (libX.so -> libX.so.<major> -> libX.so.<ver>),
        # matching -Wl,-soname,libX.so.<major>.
        set_target_properties(${_name} PROPERTIES
            VERSION ${VERSION}
            SOVERSION ${_vmajor})
        # common.mk's SHARE_FLAGS: forbid unresolved symbols.
        target_link_options(${_name} PRIVATE
            $<$<PLATFORM_ID:Linux>:LINKER:--no-undefined>
            ${SHARE_FLAGS})
    elseif(TYPE STREQUAL "EXE")
        add_executable(${_name} ${_srcs})
        if(DEFINED EXE_FLAGS)
            target_link_options(${_name} PRIVATE ${EXE_FLAGS})
        endif()
    else()
        message(FATAL_ERROR
            "super_module: unsupported TYPE '${TYPE}' "
            "(expected EXE, STATIC, SHARE, REF or NONE)")
    endif()

    _super_apply_common(${_name} ${_name})
    _super_apply_naming(${_name})

    # ---- Versioned artifact + symlink chain for STATIC and EXE -----------
    #   Shared libraries already get libX.so -> .so.<major> -> .so.<version>
    #   natively (via VERSION/SOVERSION above).  Give static archives and
    #   executables the same treatment, matching common.mk's TARGET_LINK_NAME
    #   chain: the real file carries `.<version>` and the base/major names are
    #   symlinks pointing at it.
    if(TYPE STREQUAL "STATIC" OR TYPE STREQUAL "EXE")
        # On-disk prefix: matches CMake's default (lib for archives, none for
        # executables) unless LIB_PREFIX overrides it.
        if(DEFINED LIB_PREFIX)
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" _pfx "${LIB_PREFIX}")
        elseif(TYPE STREQUAL "STATIC")
            set(_pfx "lib")
        else()
            set(_pfx "")
        endif()

        # Base suffix before the version: STATIC_SUFFIX (default .a) for
        # archives, BIN_SUFFIX (default empty) for executables.
        if(TYPE STREQUAL "STATIC")
            if(DEFINED STATIC_SUFFIX AND NOT STATIC_SUFFIX STREQUAL "")
                set(_bsuf "${STATIC_SUFFIX}")
            else()
                set(_bsuf ".a")
            endif()
        else()
            if(DEFINED BIN_SUFFIX)
                set(_bsuf "${BIN_SUFFIX}")
            else()
                set(_bsuf "")
            endif()
        endif()

        # Make CMake emit the real file as <prefix><name><base_suffix>.<version>.
        set_target_properties(${_name} PROPERTIES SUFFIX "${_bsuf}.${VERSION}")

        _super_version_links(${_name} "${_pfx}${_name}${_bsuf}" "${VERSION}" "${_vmajor}")
    endif()

    # Release symbol stripping for the shippable artifacts (SHARE / EXE).
    if(TYPE STREQUAL "SHARE" OR TYPE STREQUAL "EXE")
        _super_split_debug(${_name})
    endif()
endfunction()
