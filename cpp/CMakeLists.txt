target_sources(${APP_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GlewBootstrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/raylib-live2d.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/raylib-live2d.h
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppDefine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppDefine.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppPal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppPal.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppAllocator_Common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/l2d/LAppAllocator_Common.hpp
)

target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
