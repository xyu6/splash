#include "texture.h"

namespace Splash {

/*************/
Texture::Texture()
{
}

/*************/
Texture::~Texture()
{
}

/*************/
template<typename DataType>
Texture& Texture::operator=(const ImageBuf pImg)
{
}

/*************/
template<typename DataType>
ImageBuf Texture::getBuffer() const
{
}

/*************/
void Texture::reset(GLenum target, GLint pLevel, GLint internalFormat, GLsizei width, GLsizei height,
                    GLint border, GLenum format, GLenum type, const GLvoid* data)
{
}

} // end of namespace
