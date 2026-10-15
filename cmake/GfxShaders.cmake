# ==============================================================================
# gfx_add_shaders(): compiles GLSL and HLSL shaders to SPIR-V at build time and embeds them in a
# C++ module of the target, as ready-to-use gfx::ShaderModuleDescription constants.
# ==============================================================================
include_guard(GLOBAL)

find_program(GFX_GLSLANG_EXECUTABLE NAMES glslangValidator glslang
    DOC "glslang compiler used by gfx_add_shaders() (GLSL and HLSL to SPIR-V)")
set(GFX_EMBED_SPIRV_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/scripts/GfxEmbedSpirv.cmake")

# Shader stage of each file extension: <extension>=<gfx::ShaderStage enumerator>.
set(_GFX_SHADER_STAGES
    vert=VERTEX tesc=TESSELLATION_CONTROL tese=TESSELLATION_EVALUATION geom=GEOMETRY frag=FRAGMENT
    comp=COMPUTE task=TASK mesh=MESH rgen=RAY_GENERATION rint=INTERSECTION rahit=ANY_HIT
    rchit=CLOSEST_HIT rmiss=MISS rcall=CALLABLE)

# gfx_add_shaders(<target>
#     SHADERS <file>...               GLSL: <name>.<stage>[.glsl]   HLSL: <name>.<stage>.hlsl
#                                     <stage>: vert tesc tese geom frag comp task mesh rgen rint rahit
#                                              rchit rmiss rcall
#     [MODULE <module name>]          C++ module to generate (default: <target>.shaders)
#     [NAMESPACE <namespace>]         namespace of its constants (default: the module name, . -> ::)
#     [INCLUDE_DIRECTORIES <dir>...]  where #include looks (after the including file's directory)
#     [DEFINES <name[=value]>...]     preprocessor definitions
#     [HLSL_ENTRY_POINT <name>]       entry point of HLSL shaders (default: main)
#     [PUBLIC])                       export the module to the target's consumers (default: private)
#
# The module exports, for a shader `blur.comp`:
#   inline constexpr std::array<std::uint32_t, N> blur_comp_spirv;   its SPIR-V words
#   inline constexpr gfx::ShaderModuleDescription blur_comp;         stage, SPIR-V, entry point, name
# In Debug builds the descriptions also carry the source path, defines and include directories, so
# Device::poll_shader_changes() recompiles a shader when its source (or an included file) changes.
# Shaders are rebuilt when they or any file they include change (depfiles). Debug builds embed
# source-level debug information (-g: line information and the source text) for RenderDoc and other
# debuggers. (NonSemantic debug information, -gVS, is not used: glslang 16.2 emits it invalid for the
# built-in gl_PerVertex block of vertex shaders.)
function(gfx_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 argument "PUBLIC" "MODULE;NAMESPACE;HLSL_ENTRY_POINT"
                          "SHADERS;INCLUDE_DIRECTORIES;DEFINES")
    if(argument_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gfx_add_shaders(${target}): unknown arguments ${argument_UNPARSED_ARGUMENTS}.")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "gfx_add_shaders(${target}): ${target} is not a target.")
    endif()
    if(NOT argument_SHADERS)
        message(FATAL_ERROR "gfx_add_shaders(${target}): no SHADERS were given.")
    endif()

    # The compiler: glslang from the system, or the one built from source.
    if(GFX_GLSLANG_EXECUTABLE)
        set(compiler "${GFX_GLSLANG_EXECUTABLE}")
    elseif(TARGET glslang-standalone)
        set(compiler "$<TARGET_FILE:glslang-standalone>")
    else()
        message(FATAL_ERROR
            "gfx_add_shaders(${target}): glslang's compiler (glslangValidator) was not found.\n"
            "  Suggestion: install glslang (MSYS2: mingw-w64-ucrt-x86_64-glslang) or set GFX_GLSLANG_EXECUTABLE.")
    endif()

    # Names of the module and of its namespace.
    if(NOT argument_MODULE)
        set(argument_MODULE "${target}.shaders")
    endif()
    if(NOT argument_NAMESPACE)
        string(REPLACE "." "::" argument_NAMESPACE "${argument_MODULE}")
    endif()
    if(NOT argument_HLSL_ENTRY_POINT)
        set(argument_HLSL_ENTRY_POINT "main")
    endif()

    # Options shared by every shader: include directories (absolute) and definitions.
    set(include_options "")
    set(include_directories "")
    foreach(directory IN LISTS argument_INCLUDE_DIRECTORIES)
        cmake_path(ABSOLUTE_PATH directory BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        list(APPEND include_options "-I${directory}")
        list(APPEND include_directories "${directory}")
    endforeach()
    set(define_options "")
    foreach(definition IN LISTS argument_DEFINES)
        list(APPEND define_options "-D${definition}")
    endforeach()

    set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/gfx_shaders/${target}")
    file(MAKE_DIRECTORY "${output_directory}")
    set(manifest "")
    set(identifiers "")
    set(spirv_files "")
    foreach(shader IN LISTS argument_SHADERS)
        # The file and its stage.
        cmake_path(ABSOLUTE_PATH shader BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE source)
        cmake_path(GET source FILENAME file_name)
        if(NOT file_name MATCHES "^(.+)\\.(vert|tesc|tese|geom|frag|comp|task|mesh|rgen|rint|rahit|rchit|rmiss|rcall)(\\.(glsl|hlsl))?$")
            message(FATAL_ERROR
                "gfx_add_shaders(${target}): cannot tell the stage of '${file_name}'.\n"
                "  Suggestion: name GLSL shaders <name>.<stage>[.glsl] and HLSL shaders <name>.<stage>.hlsl, with\n"
                "              <stage> one of vert tesc tese geom frag comp task mesh rgen rint rahit rchit rmiss rcall.")
        endif()
        set(base_name "${CMAKE_MATCH_1}")
        set(extension "${CMAKE_MATCH_2}")
        set(language "${CMAKE_MATCH_4}")
        foreach(pair IN LISTS _GFX_SHADER_STAGES)
            if(pair MATCHES "^${extension}=(.+)$")
                set(stage "${CMAKE_MATCH_1}")
            endif()
        endforeach()

        # The C++ identifier of its constants.
        string(MAKE_C_IDENTIFIER "${base_name}_${extension}" identifier)
        string(TOLOWER "${identifier}" identifier)
        if(identifier IN_LIST identifiers)
            message(FATAL_ERROR "gfx_add_shaders(${target}): two shaders are named ${identifier}; rename one of them.")
        endif()
        list(APPEND identifiers "${identifier}")

        # Compile it to SPIR-V for Vulkan 1.3 (SPIR-V 1.6), with a depfile listing what it includes.
        set(spirv "${output_directory}/${file_name}.spv")
        set(depfile "${output_directory}/${file_name}.d")
        set(entry_point "main")
        set(language_options "")
        if(language STREQUAL "hlsl")
            set(entry_point "${argument_HLSL_ENTRY_POINT}")
            set(language_options -D -e "${entry_point}")
        endif()
        add_custom_command(
            OUTPUT "${spirv}"
            COMMAND "${compiler}" -V --target-env vulkan1.3 -S "${extension}" --quiet ${language_options}
                    ${include_options} ${define_options} "$<$<CONFIG:Debug>:-g>"
                    --depfile "${depfile}" -o "${spirv}" "${source}"
            DEPENDS "${source}"
            DEPFILE "${depfile}"
            COMMENT "Compiling shader ${file_name} to SPIR-V"
            COMMAND_EXPAND_LISTS
            VERBATIM)
        list(APPEND spirv_files "${spirv}")
        string(APPEND manifest
            "set(GFX_SHADER_${identifier}_STAGE \"${stage}\")\n"
            "set(GFX_SHADER_${identifier}_SPIRV \"${spirv}\")\n"
            "set(GFX_SHADER_${identifier}_SOURCE \"${source}\")\n"
            "set(GFX_SHADER_${identifier}_NAME \"${file_name}\")\n"
            "set(GFX_SHADER_${identifier}_ENTRY_POINT \"${entry_point}\")\n")
    endforeach()

    # The manifest the embedding script reads (rewritten only when it changes).
    string(PREPEND manifest
        "set(GFX_SHADER_TARGET \"${target}\")\n"
        "set(GFX_SHADER_MODULE \"${argument_MODULE}\")\n"
        "set(GFX_SHADER_NAMESPACE \"${argument_NAMESPACE}\")\n"
        "set(GFX_SHADER_IDENTIFIERS \"${identifiers}\")\n"
        "set(GFX_SHADER_DEFINES \"${argument_DEFINES}\")\n"
        "set(GFX_SHADER_INCLUDE_DIRECTORIES \"${include_directories}\")\n")
    set(manifest_file "${output_directory}/manifest.cmake")
    file(CONFIGURE OUTPUT "${manifest_file}" CONTENT "${manifest}" @ONLY)

    # The module embedding every shader.
    string(REPLACE "." "_" module_file_name "${argument_MODULE}")
    set(module_file "${output_directory}/${module_file_name}.cppm")
    add_custom_command(
        OUTPUT "${module_file}"
        COMMAND "${CMAKE_COMMAND}" "-DMANIFEST=${manifest_file}" "-DOUTPUT=${module_file}" "-DCONFIG=$<CONFIG>"
                -P "${GFX_EMBED_SPIRV_SCRIPT}"
        DEPENDS ${spirv_files} "${manifest_file}" "${GFX_EMBED_SPIRV_SCRIPT}"
        COMMENT "Embedding the SPIR-V of ${target} in module ${argument_MODULE}"
        VERBATIM)
    set(visibility PRIVATE)
    if(argument_PUBLIC)
        set(visibility PUBLIC)
    endif()
    target_sources(${target} ${visibility}
        FILE_SET gfx_shaders TYPE CXX_MODULES
        BASE_DIRS "${output_directory}"
        FILES "${module_file}")
endfunction()
