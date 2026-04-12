cmake_minimum_required(VERSION 3.16)
set(APP_NAME openv)

# --- Path Settings ---
set(SDK_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CubismSdkForNative)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)
set(RES_PATH ${SDK_ROOT_PATH}/Samples/Resources)
set(SAMPLE_SHADER_PATH ${SDK_ROOT_PATH}/Samples/OpenGL/Shaders/Standard)
set(FRAMEWORK_SHADER_PATH ${FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard)

project(${APP_NAME})

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find Packages (System Wide) ---
find_package(GLEW REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(raylib REQUIRED)
find_package(OpenGL REQUIRED)

# --- Cubism Core ---
add_library(Live2DCubismCore STATIC IMPORTED)
if(WIN32)
    set(CUBISM_CORE_CANDIDATE_LIBS
        ${CORE_PATH}/lib/windows/x86_64/Live2DCubismCore.lib
        ${CORE_PATH}/lib/windows/x86_64/libLive2DCubismCore.a
    )
    set(CUBISM_CORE_LIB "")
    foreach(_lib ${CUBISM_CORE_CANDIDATE_LIBS})
        if(EXISTS ${_lib})
            set(CUBISM_CORE_LIB ${_lib})
            break()
        endif()
    endforeach()
    if(CUBISM_CORE_LIB STREQUAL "")
        message(FATAL_ERROR "Windows Cubism Core library not found. Expected one of: ${CUBISM_CORE_CANDIDATE_LIBS}")
    endif()
else()
    set(CUBISM_CORE_LIB ${CORE_PATH}/lib/linux/x86_64/libLive2DCubismCore.a)
endif()
set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION ${CUBISM_CORE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
)

# --- Cubism Framework ---
set(FRAMEWORK_SOURCE OpenGL)
set(CUBISM_RENDER_BACKEND none)
add_subdirectory(${FRAMEWORK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/Framework)
if(WIN32)
    target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
else()
    target_compile_definitions(Framework PUBLIC CSM_TARGET_LINUX_GL)
endif()
target_include_directories(Framework PUBLIC ${GLEW_INCLUDE_DIRS})
target_link_libraries(Framework Live2DCubismCore GLEW::GLEW glfw)

# --- Raylib Renderer (must be a separate static lib so its symbols win over Framework) ---
add_library(RaylibRenderer STATIC
    cpp/render/CubismRenderer_Raylib.cpp
    cpp/render/CubismShader_Raylib.cpp
    cpp/render/CubismClippingContext.cpp
)
target_include_directories(RaylibRenderer PUBLIC
    cpp
    cpp/render
)
target_link_libraries(RaylibRenderer PUBLIC Framework raylib GLEW::GLEW)

# --- Executable ---
add_executable(${APP_NAME} cpp/main.cpp)
add_subdirectory(cpp)

target_link_libraries(${APP_NAME}
    -Wl,--whole-archive
    RaylibRenderer
    -Wl,--no-whole-archive
    Framework
    glfw
    GLEW::GLEW
    ${OPENGL_LIBRARIES}
    raylib
)

if(UNIX AND NOT APPLE)
    target_link_libraries(${APP_NAME} rt)
endif()

target_include_directories(${APP_NAME} PRIVATE ${STB_PATH})

# --- Resource Copy ---
add_custom_command(
    TARGET ${APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/Resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SAMPLE_SHADER_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/SampleShaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${FRAMEWORK_SHADER_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/FrameworkShaders
)

# Bundle facecap helper beside renderer for end-user launch.
if(WIN32)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/openv-facecap.exe")
        add_custom_command(
            TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/bin/openv-facecap.exe
                    $<TARGET_FILE_DIR:${APP_NAME}>/openv-facecap.exe
        )
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/openv.exe")
        add_custom_command(
            TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/bin/openv.exe
                    $<TARGET_FILE_DIR:${APP_NAME}>/openv-facecap.exe
        )
    endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/openv-facecap")
    add_custom_command(
        TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/bin/openv-facecap
                $<TARGET_FILE_DIR:${APP_NAME}>/openv-facecap
    )
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/openv")
    add_custom_command(
        TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/bin/openv
                $<TARGET_FILE_DIR:${APP_NAME}>/openv-facecap
    )
endif()
