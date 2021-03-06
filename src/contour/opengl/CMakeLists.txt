if("${OpenGL_GL_PREFERENCE}" STREQUAL "")
    # possible values are: GLVND, LEGACY
    set(OpenGL_GL_PREFERENCE "GLVND")
endif()

if(CONTOUR_BUILD_WITH_QT6)
   find_package(Qt6 COMPONENTS Core Gui OpenGL OpenGLWidgets Widgets REQUIRED)
else()
   find_package(Qt5 COMPONENTS Gui Widgets REQUIRED)  # apt install qtbase5-dev libqt5gui5
endif()

find_package(OpenGL REQUIRED)

CIncludeMe(shaders/background.frag "${CMAKE_CURRENT_BINARY_DIR}/background_frag.h" "background_frag" "default_shaders")
CIncludeMe(shaders/background.vert "${CMAKE_CURRENT_BINARY_DIR}/background_vert.h" "background_vert" "default_shaders")
CIncludeMe(shaders/text.frag "${CMAKE_CURRENT_BINARY_DIR}/text_frag.h" "text_frag" "default_shaders")
CIncludeMe(shaders/text.vert "${CMAKE_CURRENT_BINARY_DIR}/text_vert.h" "text_vert" "default_shaders")

add_library(contour_frontend_opengl
    "${CMAKE_CURRENT_BINARY_DIR}/background_frag.h"
    "${CMAKE_CURRENT_BINARY_DIR}/background_vert.h"
    "${CMAKE_CURRENT_BINARY_DIR}/text_frag.h"
    "${CMAKE_CURRENT_BINARY_DIR}/text_vert.h"
    OpenGLRenderer.cpp OpenGLRenderer.h
    ShaderConfig.cpp ShaderConfig.h
    TerminalWidget.cpp TerminalWidget.h
)

if(CONTOUR_PERF_STATS)
    target_compile_definitions(contour_frontend_opengl PRIVATE CONTOUR_PERF_STATS=1)
endif()

target_include_directories(contour_frontend_opengl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/../..")
target_link_libraries(contour_frontend_opengl terminal_renderer OpenGL::GL)
if(CONTOUR_BUILD_WITH_QT6)
   target_link_libraries(contour_frontend_opengl Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Widgets)
else()
   target_link_libraries(contour_frontend_opengl Qt5::Gui Qt5::Widgets)
endif()
set_target_properties(contour_frontend_opengl PROPERTIES AUTOMOC ON)
