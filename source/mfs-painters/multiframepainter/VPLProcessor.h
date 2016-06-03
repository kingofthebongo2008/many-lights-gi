#pragma once

#include <glm/mat4x4.hpp>

#include <globjects/base/ref_ptr.h>

namespace globjects
{
    class Buffer;
    class Program;
}

class RasterizationStage;


class VPLProcessor
{
public:
    VPLProcessor();
    ~VPLProcessor();

    void process(const RasterizationStage& rsmRenderer, float lightIntensity);

    globjects::ref_ptr<globjects::Buffer> vplBuffer;
    glm::mat4 biasedShadowTransform;

private:
    globjects::ref_ptr<globjects::Program> m_program;
};