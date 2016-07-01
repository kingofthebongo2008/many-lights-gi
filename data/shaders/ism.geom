#version 430

#extension GL_ARB_shading_language_include : require
#include </data/shaders/common/random.glsl>
#include </data/shaders/common/ism_utils.glsl>
#include </data/shaders/common/floatpacking.glsl>

uniform ivec2 viewport;
uniform float zFar;

struct VPL {
    vec4 position;
    vec4 color;
};

const int totalVplCount = 1024;
layout (std140, binding = 0) uniform vplBuffer_
{
    VPL vplBuffer[totalVplCount];
};


layout(points) in;
layout(points, max_vertices = 1) out;



uniform int vplStartIndex = 0;
uniform int vplEndIndex = totalVplCount;
uniform bool scaleISMs = false;
uniform bool pointsOnlyIntoScaledISMs = false;

int vplCount = vplEndIndex - vplStartIndex;
int sampledVplCount = pointsOnlyIntoScaledISMs ? vplCount : totalVplCount;
int ismCount = (scaleISMs) ? vplCount : totalVplCount;
int ismIndices1d = int(ceil(sqrt(ismCount)));
int vplIdOffset = pointsOnlyIntoScaledISMs ? vplStartIndex : 0;

const float infinity = 1. / 0.;


void main()
{
    vec4 position = gl_in[0].gl_Position;

    int vplID = (int(rand(position.xyz) * sampledVplCount) + gl_PrimitiveIDIn) % sampledVplCount;
    if (pointsOnlyIntoScaledISMs)
        vplID += vplIdOffset;

    // vec3 vplNormal = vplBuffer[vplID].normal.xyz;
    vec3 vplPosition = vplBuffer[vplID].position.xyz;
    vec3 vplNormal = Unpack3PNFromFP32(vplBuffer[vplID].position.w).xyz * 2.0 - 1.0;
    mat3 vplView = lookAtRH(vplNormal);
    // vplNormal = unpackedNormal;

    // culling
    vec3 positionRelativeToCamera = position.xyz - vplPosition;
    vec3 viewCoord = vplView * positionRelativeToCamera;
    if (viewCoord.z > 0.0) {
        viewCoord.z = infinity;
        return; // makes it slower
    }

    // paraboloid projection
    float distToCamera = length(positionRelativeToCamera);
    float ismIndex = scaleISMs ? float(vplID) - vplStartIndex : vplID;
    vec3 v = paraboloid_project(positionRelativeToCamera, distToCamera, vplNormal, zFar, ismIndex, ismIndices1d);

    // to tex and NDC coords
    v.xy = v.xy * 2.0 - 1.0;
    v.z = v.z * 2.0 - 1.0;


    gl_Position = vec4(0.5 * length(vplNormal + vplPosition) * 0.0000001, 0.5, 0.5, 1.0);
    gl_Position = vec4(v, 1.0);

    float pointsPerMeter = 20.0; // actual number unknown, needs to be calculated during tesselation
    float pointSize = 1.0 / pointsPerMeter / distToCamera / 3.14 * viewport.x; // approximation that breaks especially for near points.
    float maximumPointSize = 10.0;
    pointSize = min(pointSize, maximumPointSize);

    gl_PointSize = pointSize;
    EmitVertex();
}
