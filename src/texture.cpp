#include "texture.h"
#include "threadpool.h"
#include "timer.h"

#include <string>

#define SPLASH_TEXTURE_COPY_THREADS 4

using namespace std;
using namespace OIIO_NAMESPACE;

namespace Splash {

/*************/
Texture::Texture()
{
    init();
}

/*************/
Texture::Texture(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                 GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    init();
    reset(target, level, internalFormat, width, height, border, format, type, data); 
}

/*************/
Texture::~Texture()
{
#ifdef DEBUG
    SLog::log << Log::DEBUGGING << "Texture::~Texture - Destructor" << Log::endl;
#endif
    glDeleteTextures(1, &_glTex);
}

/*************/
Texture& Texture::operator=(ImagePtr& img)
{
    _img = img;
    return *this;
}

/*************/
void Texture::generateMipmap() const
{
    glBindTexture(GL_TEXTURE_2D, _glTex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/*************/
bool Texture::linkTo(BaseObjectPtr obj)
{
    if (dynamic_pointer_cast<Image>(obj).get() != nullptr)
    {
        ImagePtr img = dynamic_pointer_cast<Image>(obj);
        _img = img;
        return true;
    }

    return false;
}

/*************/
ImagePtr Texture::read()
{
    ImagePtr img = make_shared<Image>(_spec);
    glBindTexture(GL_TEXTURE_2D, _glTex);
    glGetTexImage(GL_TEXTURE_2D, 0, _texFormat, _texType, (GLvoid*)img->data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return img;
}

/*************/
void Texture::reset(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                    GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    if (width == 0 || height == 0)
    {
        SLog::log << Log::WARNING << "Texture::" << __FUNCTION__ << " - Texture size is null" << Log::endl;
        return;
    }

    if (_glTex == 0)
    {
        glGenTextures(1, &_glTex);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _glTex);

        if (internalFormat == GL_DEPTH_COMPONENT)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            if (_filtering)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
            else
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }

            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        }

        glTexImage2D(target, level, internalFormat, width, height, border, format, type, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    else
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _glTex);
        glTexImage2D(target, level, internalFormat, width, height, border, format, type, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    _spec.width = width;
    _spec.height = height;
    if (internalFormat == GL_RGB && type == GL_UNSIGNED_BYTE)
    {
        _spec.nchannels = 3;
        _spec.format = TypeDesc::UINT8;
        _spec.channelnames = {"R", "G", "B"};
    }
    else if (internalFormat == GL_RGBA && (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV))
    {
        _spec.nchannels = 4;
        _spec.format = TypeDesc::UINT8;
        _spec.channelnames = {"R", "G", "B", "A"};
    }
    else if (internalFormat == GL_RGBA16 && type == GL_UNSIGNED_SHORT)
    {
        _spec.nchannels = 4;
        _spec.format = TypeDesc::UINT16;
        _spec.channelnames = {"R", "G", "B", "A"};
    }
    else if (internalFormat == GL_R && type == GL_UNSIGNED_SHORT)
    {
        _spec.nchannels = 1;
        _spec.format = TypeDesc::UINT16;
        _spec.channelnames = {"R"};
    }

    _texTarget = target;
    _texLevel = level;
    _texInternalFormat = internalFormat;
    _texBorder = border;
    _texFormat = format;
    _texType = type;

#ifdef DEBUG
    SLog::log << Log::DEBUGGING << "Texture::" << __FUNCTION__ << " - Reset the texture to size " << width << "x" << height << Log::endl;
#endif
}

/*************/
void Texture::resize(int width, int height)
{
    if (width != _spec.width && height != _spec.height)
        reset(_texTarget, _texLevel, _texInternalFormat, width, height, _texBorder, _texFormat, _texType, 0);
}

/*************/
void Texture::update()
{
    lock_guard<mutex> lock(_mutex);

    // If _img is nullptr, this texture is not set from an Image
    if (_img.get() == nullptr)
        return;

    if (_img->getTimestamp() == _timestamp)
        return;
    _img->update();

    ImageSpec spec = _img->getSpec();
    vector<Value> srgb;
    _img->getAttribute("srgb", srgb);

    if (!(bool)glIsTexture(_glTex))
    {
        glGenTextures(1, &_glTex);
        return;
    }

    // Store the image data size
    int imageDataSize = spec.width * spec.height * spec.pixel_bytes();

    GLint glChannelOrder;
    if (spec.channelnames == vector<string>({"B", "G", "R"}))
        glChannelOrder = GL_BGR;
    else if (spec.channelnames == vector<string>({"B", "G", "R", "A"}))
        glChannelOrder = GL_BGRA;
    else if (spec.channelnames == vector<string>({"R", "G", "B"})
          || spec.channelnames == vector<string>({"RGB_DXT1"}))
        glChannelOrder = GL_RGB;
    else if (spec.channelnames == vector<string>({"R", "G", "B", "A"})
          || spec.channelnames == vector<string>({"RGBA_DXT5"}))
        glChannelOrder = GL_RGBA;
    else if (spec.nchannels == 3)
        glChannelOrder = GL_RGB;
    else if (spec.nchannels == 4)
        glChannelOrder = GL_RGBA;

    // If the texture is compressed, we need to modify a few values
    bool isCompressed = false;
    if (spec.channelnames == vector<string>({"RGB_DXT1"}))
    {
        isCompressed = true;
        spec.height *= 2;
        spec.nchannels = 3;
    }
    else if (spec.channelnames == vector<string>({"RGBA_DXT5"}))
    {
        isCompressed = true;
        spec.nchannels = 4;
    }

    // Update the textures if the format changed
    if (spec.width != _spec.width || spec.height != _spec.height || spec.nchannels != _spec.nchannels || spec.format != _spec.format)
    {
        glBindTexture(GL_TEXTURE_2D, _glTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        if (_filtering)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        else
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }

        if (spec.nchannels == 4 && spec.format == "uint8" && !isCompressed)
        {
#ifdef DEBUG
            SLog::log << Log::DEBUGGING << "Texture::" <<  __FUNCTION__ << " - Creating a new texture of type GL_UNSIGNED_BYTE, format GL_RGBA (source RGBA)" << Log::endl;
#endif
            _img->lock();
            if (srgb[0].asInt() > 0)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, spec.width, spec.height, 0, glChannelOrder, GL_UNSIGNED_INT_8_8_8_8_REV, _img->data());
            else
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, spec.width, spec.height, 0, glChannelOrder, GL_UNSIGNED_INT_8_8_8_8_REV, _img->data());
            _img->unlock();
        }
        else if (spec.nchannels == 3 && spec.format == "uint8" && !isCompressed)
        {
#ifdef DEBUG
            SLog::log << Log::DEBUGGING << "Texture::" <<  __FUNCTION__ << " - Creating a new texture of type GL_UNSIGNED_BYTE, format GL_RGBA (source RGB)" << Log::endl;
#endif
            _img->lock();
            if (srgb[0].asInt() > 0)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, spec.width, spec.height, 0, glChannelOrder, GL_UNSIGNED_BYTE, _img->data());
            else
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, spec.width, spec.height, 0, glChannelOrder, GL_UNSIGNED_BYTE, _img->data());
            _img->unlock();
        }
        else if (spec.nchannels == 1 && spec.format == "uint16" && !isCompressed)
        {
#ifdef DEBUG
            SLog::log << Log::DEBUGGING << "Texture::" <<  __FUNCTION__ << " - Creating a new texture of type GL_UNSIGNED_SHORT, format GL_R" << Log::endl;
#endif
            _img->lock();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, spec.width, spec.height, 0, GL_RED, GL_UNSIGNED_SHORT, _img->data());
            _img->unlock();
        }
        else if (spec.channelnames == vector<string>({"RGB_DXT1"}))
        {
#ifdef DEBUG
            SLog::log << Log::DEBUGGING << "Texture::" <<  __FUNCTION__ << " - Creating a new texture of type GL_COMPRESSED_RGB_S3TC_DXT1, format GL_RGBA (source RGBA)" << Log::endl;
#endif
            _img->lock();
            if (srgb[0].asInt() > 0)
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, spec.width, spec.height, 0, imageDataSize, _img->data());
            else
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, spec.width, spec.height, 0, imageDataSize, _img->data());
            _img->unlock();
        }
        else if (spec.channelnames == vector<string>({"RGBA_DXT5"}) || spec.channelnames == vector<string>({"YCoCg_DXT5"}))
        {
#ifdef DEBUG
            SLog::log << Log::DEBUGGING << "Texture::" <<  __FUNCTION__ << " - Creating a new texture of type GL_COMPRESSED_RGBA_S3TC_DXT5, format GL_RGBA (source RGBA)" << Log::endl;
#endif
            _img->lock();
            if (srgb[0].asInt() > 0)
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, spec.width, spec.height, 0, imageDataSize, _img->data());
            else
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, spec.width, spec.height, 0, imageDataSize, _img->data());
            _img->unlock();
        }
        else
        {
            SLog::log << Log::WARNING << "Texture::" <<  __FUNCTION__ << " - Texture format not supported" << Log::endl;
            return;
        }
        updatePbos(spec.width, spec.height, spec.pixel_bytes());

        // Fill one of the PBOs right now
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _pbos[_pboReadIndex]);
        GLubyte* pixels = (GLubyte*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        if (pixels != NULL)
        {
            _img->lock();
            memcpy((void*)pixels, _img->data(), imageDataSize);
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            _img->unlock();
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        //glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        _spec = spec;
    }
    // Update the content of the texture, i.e the image
    else
    {
        glBindTexture(GL_TEXTURE_2D, _glTex);

        // Copy the pixels from the current PBO to the texture
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _pbos[_pboReadIndex]);
        if (spec.nchannels == 4 && spec.format == "uint8" && !isCompressed)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, glChannelOrder, GL_UNSIGNED_BYTE, 0);
        else if (spec.nchannels == 3 && spec.format == "uint8" && !isCompressed)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, glChannelOrder, GL_UNSIGNED_BYTE, 0);
        else if (spec.nchannels == 1 && spec.format == "uint16" && !isCompressed)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, GL_RED, GL_UNSIGNED_SHORT, 0);
        else if (spec.channelnames == vector<string>({"RGB_DXT1"}))
            if (srgb[0].asInt() > 0)
                glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, imageDataSize, 0);
            else
                glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, imageDataSize, 0);
        else if (spec.channelnames == vector<string>({"RGBA_DXT5"}) || spec.channelnames == vector<string>({"YCoCg_DXT5"}))
            if (srgb[0].asInt() > 0)
                glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, imageDataSize, 0);
            else
                glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, spec.width, spec.height, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, imageDataSize, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        //glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        _pboReadIndex = (_pboReadIndex + 1) % 2;
        
        // Fill the next PBO with the image pixels
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _pbos[_pboReadIndex]);
        GLubyte* pixels = (GLubyte*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        if (pixels != NULL)
        {
            _img->lock();

            _pboCopyThreadIds.clear();
            int stride = SPLASH_TEXTURE_COPY_THREADS;
            int size = imageDataSize;
            for (int i = 0; i < stride - 1; ++i)
            {
                _pboCopyThreadIds.push_back(SThread::pool.enqueue([=]() {
                    copy((char*)_img->data() + size / stride * i, (char*)_img->data() + size / stride * (i + 1), (char*)pixels + size / stride * i);
                }));
            }
            _pboCopyThreadIds.push_back(SThread::pool.enqueue([=]() {
                copy((char*)_img->data() + size / stride * (stride - 1), (char*)_img->data() + size, (char*)pixels + size / stride * (stride - 1));
            }));
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    // If needed, specify some uniforms for the shader which will use this texture
    _shaderUniforms.clear();
    if (spec.channelnames == vector<string>({"YCoCg_DXT1"}))
        _shaderUniforms["YCoCg"] = Values({1});
    else
        _shaderUniforms["YCoCg"] = Values({0});

    _timestamp = _img->getTimestamp();

}

/*************/
void Texture::flushPbo()
{
    if (_pboCopyThreadIds.size() != 0)
    {
        SThread::pool.waitThreads(_pboCopyThreadIds);
        _pboCopyThreadIds.clear();

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _pbos[_pboReadIndex]);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        _img->unlock();
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
}

/*************/
void Texture::init()
{
    _type = "texture";
    _timestamp = chrono::high_resolution_clock::now();

    glGenBuffers(2, _pbos);
}

/*************/
void Texture::updatePbos(int width, int height, int bytes)
{
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _pbos[0]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, width * height * bytes, 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _pbos[1]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, width * height * bytes, 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

} // end of namespace
