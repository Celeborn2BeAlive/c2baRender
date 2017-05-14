cmake_minimum_required(VERSION 2.8)

project(c2baRender)

if(CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(
        -std=c++14
    )
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(IMGUI_DIR imgui)
set(GLM_DIR glm)
set(GLAD_DIR glad)
set(JSON_DIR json)
set(STB_DIR stb)

find_package(OpenGL REQUIRED)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(c2ba-glsl-shaders)
include(c2ba-copy-dll-post-build)

if(MSVC)
    set(SHADER_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin/\$\(Configuration\)/shaders)
else()
    set(SHADER_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin/shaders)
endif()

if(MSVC)
    set(DIST_PATH ${CMAKE_SOURCE_DIR}/third-party/dist/\$\(Configuration\))
else()
    set(DIST_PATH ${CMAKE_SOURCE_DIR}/third-party/dist/${CMAKE_BUILD_TYPE})
endif()

include_directories(
    ${OPENGL_INCLUDE_DIRS}
    ${DIST_PATH}/include
    third-party/src/${GLM_DIR}
    third-party/src/${GLAD_DIR}/include
    third-party/src/${JSON_DIR}
    third-party/src/${IMGUI_DIR}
    third-party/src/${STB_DIR}
    third-party/src
    libs/c2ba/include
)

set(
    IMGUI_SRC_FILES
    third-party/src/${IMGUI_DIR}/imgui.cpp
    third-party/src/${IMGUI_DIR}/imgui_demo.cpp
    third-party/src/${IMGUI_DIR}/imgui_draw.cpp
)

set(
    THIRD_PARTY_SRC_FILES
    ${IMGUI_SRC_FILES}
    third-party/src/${GLAD_DIR}/src/glad.c
)

file(
    GLOB_RECURSE
    C2BA_LIB_FILES
    libs/c2ba/*.cpp lib/c2ba/*.hpp lib/c2ba/*.glsl
    ${THIRD_PARTY_SRC_FILES}
)

add_library(
    c2ba
    ${C2BA_LIB_FILES}
)

set(
    THIRD_PARTY_LIBRARIES
    ${OPENGL_LIBRARIES}
    ${DIST_PATH}/lib/glfw3.lib
    debug ${DIST_PATH}/lib/assimp-vc140-mtd.lib optimized ${DIST_PATH}/lib/assimp-vc140-mt.lib
    ${DIST_PATH}/lib/embree.lib
    ${DIST_PATH}/lib/Half.lib
    ${DIST_PATH}/lib/Iex-2_2.lib
    ${DIST_PATH}/lib/IexMath-2_2.lib
    ${DIST_PATH}/lib/IlmImf-2_2.lib
    ${DIST_PATH}/lib/IlmImfUtil-2_2.lib
    ${DIST_PATH}/lib/IlmThread-2_2.lib
    ${DIST_PATH}/lib/Imath-2_2.lib
    debug ${DIST_PATH}/lib/zlibd.lib optimized ${DIST_PATH}/lib/zlib.lib
)

target_link_libraries(
    c2ba
    ${THIRD_PARTY_LIBRARIES}
)

c2ba_add_shader_directory(${CMAKE_CURRENT_SOURCE_DIR}/libs/c2ba/shaders ${SHADER_OUTPUT_PATH}/c2ba)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/libs/c2ba/shaders/
    DESTINATION shaders/c2ba
    FILES_MATCHING PATTERN "*.glsl"
)

set(
    LIBRARIES
    ${THIRD_PARTY_LIBRARIES}
    c2ba
)

if(CMAKE_COMPILER_IS_GNUCXX)
    set(LIBRARIES ${LIBRARIES} stdc++fs)
endif()

source_group ("glsl" REGULAR_EXPRESSION "*/*.glsl")
source_group ("third-party" REGULAR_EXPRESSION "third-party/*.*")

file(GLOB APP_DIRECTORIES "apps/*")
foreach(DIR ${APP_DIRECTORIES})
    get_filename_component(APP ${DIR} NAME)

    file(
        GLOB_RECURSE
        SRC_FILES
        apps/${APP}/*.cpp apps/${APP}/*.hpp apps/${APP}/*.glsl apps/${APP}/
    )

    add_executable(
        ${APP}
        ${SRC_FILES}
        ${THIRD_PARTY_SRC_FILES}
    )

    target_link_libraries(
        ${APP}
        ${LIBRARIES}
    )

    install(
        TARGETS ${APP}
        DESTINATION .
    )

    c2ba_add_shader_directory(${DIR}/shaders ${SHADER_OUTPUT_PATH}/${APP})

    install(
        DIRECTORY ${DIR}/shaders/
        DESTINATION shaders/${APP}
        FILES_MATCHING PATTERN "*.glsl"
    )

    if (MSVC)
        file(
            GLOB_RECURSE
            DLL_FILES_DEBUG
            third-party/dist/Debug/*.dll
        )

        file(
            GLOB_RECURSE
            DLL_FILES_RELEASE
            third-party/dist/Release/*.dll
        )

        foreach(DLL_FILE ${DLL_FILES_DEBUG})
                add_custom_command(
                    TARGET ${APP}
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy ${DLL_FILE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug
                    COMMENT "copying dll ${DLL_FILE}"
                )
        endforeach()

        foreach(DLL_FILE ${DLL_FILES_RELEASE})
                add_custom_command(
                    TARGET ${APP}
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy ${DLL_FILE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release
                    COMMENT "copying dll ${DLL_FILE}"
                )
        endforeach()
    endif()

endforeach()