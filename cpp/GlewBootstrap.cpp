#include <GL/glew.h>

bool InitGlewForCubism(const char **errorText)
{
    static bool initialized = false;
    if (initialized)
    {
        return true;
    }

    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        if (errorText)
        {
            *errorText = reinterpret_cast<const char *>(glewGetErrorString(err));
        }
        return false;
    }

    // glewInit can leave a benign GL error on some drivers.
    glGetError();
    initialized = true;
    return true;
}
