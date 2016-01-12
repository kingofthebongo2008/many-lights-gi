#include "RasterizationStage.h"

#include <glbinding/gl/enum.h>
#include <glbinding/gl/functions.h>
#include <glbinding/gl/boolean.h>

#include <globjects/Framebuffer.h>
#include <globjects/Texture.h>
#include <globjects/Program.h>
#include <globjects/Shader.h>

#include <gloperate/base/make_unique.hpp>
#include <gloperate/painter/AbstractPerspectiveProjectionCapability.h>
#include <gloperate/painter/AbstractViewportCapability.h>
#include <gloperate/painter/AbstractCameraCapability.h>

#include <gloperate/primitives/PolygonalDrawable.h>

#include "TransparencyMasksGenerator.h"
#include "NoiseTexture.h"
#include "OmnidirectionalShadowmap.h"
#include "GroundPlane.h"
#include "Material.h"

using namespace gl;
using gloperate::make_unique;

namespace
{
    enum Sampler
    {
        ShadowSampler,
        MaskSampler,
        NoiseSampler,
        DiffuseSampler,
        SpecularSampler,
        EmissiveSampler,
        OpacitySampler,
        BumpSampler
    };
}

RasterizationStage::RasterizationStage()
{
    currentFrame.data() = 1;

    addInput("projection", projection);
    addInput("viewport", viewport);
    addInput("camera", camera);
    addInput("drawablesMap", drawablesMap);
    addInput("presetInformation", presetInformation);
    addInput("materialMap", materialMap);
    addInput("useReflections", useReflections);
    addInput("useDOF", useDOF);
    addInput("multiframeCount", multiFrameCount);
    addInput("antiAliasingKernel", antiAliasingKernel);
    addInput("depthOfFieldKernel", depthOfFieldKernel);
    addInput("shadowKernel", shadowKernel);

    addOutput("currentFrame", currentFrame);
    addOutput("color", color);
    addOutput("normal", normal);
    addOutput("depth", depth);
    addOutput("worldPos", worldPos);
    addOutput("reflectMask", reflectMask);
}

void RasterizationStage::initialize()
{
    setupGLState();
    setupMasksTexture();

    m_noiseTexture = make_unique<NoiseTexture>(3u, 3u);
    m_shadowmap = make_unique<OmnidirectionalShadowmap>();

    color.data() = globjects::Texture::createDefault(GL_TEXTURE_2D);
    normal.data() = globjects::Texture::createDefault(GL_TEXTURE_2D);
    worldPos.data() = globjects::Texture::createDefault(GL_TEXTURE_2D);
    reflectMask.data() = globjects::Texture::createDefault(GL_TEXTURE_2D);
    depth.data() = globjects::Texture::createDefault(GL_TEXTURE_2D);

    m_fbo = new globjects::Framebuffer();
    m_fbo->attachTexture(GL_COLOR_ATTACHMENT0, color.data());
    m_fbo->attachTexture(GL_COLOR_ATTACHMENT1, normal.data());
    m_fbo->attachTexture(GL_COLOR_ATTACHMENT2, worldPos.data());
    m_fbo->attachTexture(GL_COLOR_ATTACHMENT3, reflectMask.data());
    m_fbo->attachTexture(GL_DEPTH_ATTACHMENT, depth.data());

    m_program = new globjects::Program();
    m_program->attach(
        globjects::Shader::fromFile(GL_VERTEX_SHADER, "data/shaders/model.vert"),
        globjects::Shader::fromFile(GL_FRAGMENT_SHADER, "data/shaders/model.frag")
    );


    m_zOnlyProgram = new globjects::Program();
    m_zOnlyProgram->attach(
        globjects::Shader::fromFile(GL_VERTEX_SHADER, "data/shaders/model.vert"),
        globjects::Shader::fromFile(GL_FRAGMENT_SHADER, "data/shaders/empty.frag")
    );
}

void RasterizationStage::process()
{
    if (viewport.hasChanged())
    {
        resizeTextures(viewport.data()->width(), viewport.data()->height());
    }

    currentFrame.data() += 1;
    for (auto input : this->inputs())
    {
        if (input->hasChanged())
        {
            currentFrame.data() = 1;
            alwaysProcess(true);
        }
    }

    if (currentFrame.data() > multiFrameCount.data())
    {
        alwaysProcess(false);
        return;
    }

    if (presetInformation.hasChanged())
    {
        camera.data()->setEye(presetInformation.data().camEye);
        camera.data()->setCenter(presetInformation.data().camCenter);
        projection.data()->setZNear(presetInformation.data().nearFar.x);
        projection.data()->setZFar(presetInformation.data().nearFar.y);

        m_groundPlane = make_unique<GroundPlane>(presetInformation.data().groundHeight);
    }

    render();

    invalidateOutputs();
}

void RasterizationStage::resizeTextures(int width, int height)
{
    color.data()->image2D(0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    normal.data()->image2D(0, GL_RGBA32F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    worldPos.data()->image2D(0, GL_RGBA32F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    reflectMask.data()->image2D(0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    depth.data()->image2D(0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    
    worldPos.data()->setParameter(GLenum::GL_TEXTURE_MAG_FILTER, GLenum::GL_NEAREST);
    worldPos.data()->setParameter(GLenum::GL_TEXTURE_MIN_FILTER, GLenum::GL_NEAREST);

    m_fbo->printStatus(true);
}

void RasterizationStage::render()
{
    for (auto program : std::vector<globjects::Program*>{ m_program, m_shadowmap->program() })
    {
        program->setUniform("alpha", presetInformation.data().alpha);
    }

    auto lightPosition = presetInformation.data().lightPosition;
    auto lightRadius = presetInformation.data().lightMaxShift;

    auto frameLightOffset = shadowKernel.data()[currentFrame.data() - 1] * lightRadius;
    auto frameLightPosition = lightPosition + glm::vec3(frameLightOffset.x, 0.0f, frameLightOffset.y);

    m_shadowmap->render(frameLightPosition, drawablesMap.data(), *m_groundPlane.get(), presetInformation.data().nearFar.x, presetInformation.data().nearFar.y);

    glViewport(viewport.data()->x(),
               viewport.data()->y(),
               viewport.data()->width(),
               viewport.data()->height());

    m_fbo->bind();
    m_fbo->setDrawBuffers({
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
        GL_COLOR_ATTACHMENT3
    });

    auto maxFloat = std::numeric_limits<float>::max();

    m_fbo->clearBuffer(GL_COLOR, 0, glm::vec4(presetInformation.data().groundColor, 1.0f));
    m_fbo->clearBuffer(GL_COLOR, 1, glm::vec4(0.0f));
    m_fbo->clearBuffer(GL_COLOR, 2, glm::vec4(maxFloat));
    m_fbo->clearBuffer(GL_COLOR, 3, glm::vec4(0.0f));
    m_fbo->clearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0.0f);

    zPrepass();

    m_program->use();

    auto subpixelSample = antiAliasingKernel.data()[currentFrame.data() - 1];
    auto viewportSize = glm::vec2(viewport.data()->width(), viewport.data()->height());
    auto focalPoint = depthOfFieldKernel.data()[currentFrame.data() - 1] * presetInformation.data().focalPoint;
    focalPoint *= useDOF.data();

    for (auto program : std::vector<globjects::Program*>{ m_program, m_groundPlane->program() })
    {
        program->setUniform("shadowmap", ShadowSampler);
        program->setUniform("masksTexture", MaskSampler);
        program->setUniform("noiseTexture", NoiseSampler);
        program->setUniform("diffuseTexture", DiffuseSampler);
        program->setUniform("specularTexture", SpecularSampler);
        program->setUniform("emissiveTexture", EmissiveSampler);
        program->setUniform("opacityTexture", OpacitySampler);
        program->setUniform("bumpTexture", BumpSampler);

        program->setUniform("groundPlaneColor", presetInformation.data().groundColor);
        program->setUniform("worldLightPos", frameLightPosition);

        program->setUniform("cameraEye", camera.data()->eye());
        program->setUniform("modelView", camera.data()->view());
        program->setUniform("projection", projection.data()->projection());

        // offset needs to be doubled, because ndc range is [-1;1] and not [0;1]
        program->setUniform("ndcOffset", 2.0f * subpixelSample / viewportSize);

        program->setUniform("masksOffset", static_cast<float>(currentFrame.data()) / TransparencyMasksGenerator::s_numMasks);

        program->setUniform("cocPoint", focalPoint);
        program->setUniform("focalDist", presetInformation.data().focalDist);
    }

    m_shadowmap->distanceTexture()->bindActive(ShadowSampler);
    m_masksTexture->bindActive(MaskSampler);
    m_noiseTexture->bindActive(NoiseSampler);

    for (auto& pair : drawablesMap.data())
    {
        auto materialId = pair.first;
        auto& drawables = pair.second;

        auto& material = materialMap.data().at(materialId);

        bool hasDiffuseTex = material.hasTexture(TextureType::Diffuse);
        bool hasBumpTex = material.hasTexture(TextureType::Bump);
        bool hasSpecularTex = material.hasTexture(TextureType::Specular);
        bool hasEmissiveTex = material.hasTexture(TextureType::Emissive);
        bool hasOpacityTex = material.hasTexture(TextureType::Opacity);

        if (hasDiffuseTex)
        {
            auto tex = material.textureMap().at(TextureType::Diffuse);
            tex->bindActive(DiffuseSampler);
        }

        if (hasSpecularTex)
        {
            auto tex = material.textureMap().at(TextureType::Specular);
            tex->bindActive(SpecularSampler);
        }

        if (hasEmissiveTex)
        {
            auto tex = material.textureMap().at(TextureType::Emissive);
            tex->bindActive(EmissiveSampler);
        }

        if (hasOpacityTex)
        {
            auto tex = material.textureMap().at(TextureType::Opacity);
            tex->bindActive(OpacitySampler);
        }

        auto bumpType = BumpType::None;
        if (hasBumpTex)
        {
            bumpType = presetInformation.data().bumpType;
            auto tex = material.textureMap().at(TextureType::Bump);
            tex->bindActive(BumpSampler);
        }

        m_program->setUniform("shininess", material.specularFactor);

        m_program->setUniform("bumpType", static_cast<int>(bumpType));
        m_program->setUniform("useDiffuseTexture", hasDiffuseTex);
        m_program->setUniform("useSpecularTexture", hasSpecularTex);
        m_program->setUniform("useEmissiveTexture", hasEmissiveTex);
        m_program->setUniform("useOpacityTexture", hasOpacityTex);

        for (auto& drawable : drawables)
        {
            drawable->draw();
        }
    }

    m_program->release();

    m_groundPlane->draw();

    m_fbo->unbind();
}

void RasterizationStage::zPrepass()
{
    m_zOnlyProgram->use();

    for (auto& pair : drawablesMap.data())
    {
        auto& drawables = pair.second;

        for (auto& drawable : drawables)
        {
            drawable->draw();
        }
    }

    m_zOnlyProgram->release();
}

void RasterizationStage::setupGLState()
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void RasterizationStage::setupMasksTexture()
{
    const auto table = TransparencyMasksGenerator::generateDistributions(1);
    m_masksTexture = globjects::Texture::createDefault(GL_TEXTURE_2D);
    m_masksTexture->image2D(0, GL_R8, table->at(0).size(), table->size(), 0, GL_RED, GL_UNSIGNED_BYTE, table->data());
}
