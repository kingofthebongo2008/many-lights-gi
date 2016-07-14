#include "Shadowmap.h"

#include <limits>
#include <memory>

#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <glbinding/gl/enum.h>
#include <glbinding/gl/functions.h>
#include <glbinding/gl/bitfield.h>

#include <globjects/Shader.h>
#include <globjects/Program.h>
#include <globjects/Texture.h>
#include <globjects/Framebuffer.h>

#include <gloperate/primitives/VertexDrawable.h>
#include <gloperate/primitives/PolygonalDrawable.h>

using namespace gl;

namespace
{
    const int size = 512;
}

Shadowmap::Shadowmap()
{
    m_shadowmapProgram = new globjects::Program();
    m_shadowmapProgram->attach(
        globjects::Shader::fromFile(GL_VERTEX_SHADER, "data/shaders/shadowmapping_nonomni.vert"),
        globjects::Shader::fromFile(GL_FRAGMENT_SHADER, "data/shaders/shadowmapping.frag"));

    m_fbo = new globjects::Framebuffer();
    vsmBuffer = globjects::Texture::createDefault();
    depthBuffer = globjects::Texture::createDefault();
    setupFbo(*m_fbo, *vsmBuffer, *depthBuffer, size);

    vsmBuffer->setName("Shadowmap VSM");
    depthBuffer->setName("Shadowmap Depth");
}

Shadowmap::~Shadowmap()
{

}
void Shadowmap::setupSimpleFbo(globjects::Framebuffer& fbo, globjects::Texture& VSMBuffer, int size)
{
    VSMBuffer.bind();
    VSMBuffer.setParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    VSMBuffer.setParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glm::vec4 color(0.0, 0.0, 1.0, 0.0); // third channel is alpha
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, (float*)&color);

    VSMBuffer.storage2D(1, GL_RGBA32F, size, size);
    VSMBuffer.unbind();
    fbo.attachTexture(GL_COLOR_ATTACHMENT0, &VSMBuffer);
}

void Shadowmap::setupFbo(globjects::Framebuffer& fbo, globjects::Texture& VSMBuffer, globjects::Texture& depthBuffer, int size)
{
    setupSimpleFbo(fbo, VSMBuffer, size);


    depthBuffer.bind();
    depthBuffer.storage2D(1, GL_DEPTH_COMPONENT24, size, size);
    depthBuffer.unbind();
    fbo.attachTexture(GL_DEPTH_ATTACHMENT, &depthBuffer);

    fbo.setDrawBuffer(GL_COLOR_ATTACHMENT0);
    fbo.checkStatus();
    fbo.unbind();
}

void Shadowmap::render(const glm::vec3 &eye, const glm::mat4 &viewProjection, const IdDrawablesMap& drawablesMap, const glm::vec2& nearFar) const
{
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glViewport(0, 0, size, size);

    m_fbo->bind();
    m_fbo->setDrawBuffer(GL_COLOR_ATTACHMENT0);
    
    auto maxFloat = std::numeric_limits<float>::max();
    m_fbo->clearBuffer(GL_COLOR, 0, glm::vec4(maxFloat, maxFloat, 1.0f, 0.0f));
    m_fbo->clearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0);

    m_shadowmapProgram->setUniform("transform", viewProjection);
    m_shadowmapProgram->setUniform("lightWorldPos", eye);

    m_shadowmapProgram->use();
    for (const auto& pair : drawablesMap)
    {
        auto& drawables = pair.second;
        for (auto& drawable : drawables)
        {
            drawable->draw();
        }
    }

    m_shadowmapProgram->release();
}

globjects::Program* Shadowmap::program() const
{
    return m_shadowmapProgram;
}