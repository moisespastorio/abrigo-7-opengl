cmake_minimum_required(VERSION 3.16)
project(bunker C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Executável: seu código + o glad.c
add_executable(bunker main.c src/glad/src/glad.c)

# Onde está o glad/glad.h  (src/glad/include/glad/glad.h)
target_include_directories(bunker PRIVATE src/glad/include)

if(MSVC)
    target_compile_options(bunker PRIVATE /W3)
else()
    target_compile_options(bunker PRIVATE -Wall -O2)
endif()

# ---------------------------------------------------------------
# GLFW e cglm: usa o que já estiver instalado; se não achar,
# baixa e compila automaticamente (precisa de internet e git).
# ---------------------------------------------------------------
include(FetchContent)

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "GLFW nao encontrado, baixando...")
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4)
    FetchContent_MakeAvailable(glfw)
endif()

find_package(cglm QUIET)
if(NOT cglm_FOUND)
    message(STATUS "cglm nao encontrado, baixando...")
    set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
    set(CGLM_STATIC ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(cglm
        GIT_REPOSITORY https://github.com/recp/cglm.git
        GIT_TAG v0.9.4)
    FetchContent_MakeAvailable(cglm)
    set(CGLM_TARGET cglm)
else()
    set(CGLM_TARGET cglm::cglm)
endif()

target_link_libraries(bunker PRIVATE glfw ${CGLM_TARGET})

# No Linux o glad usa dlopen e precisa de -ldl e -lm
if(UNIX)
    target_link_libraries(bunker PRIVATE m ${CMAKE_DL_LIBS})
endif()