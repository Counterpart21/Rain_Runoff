// Original RainRunoff asset generator. See ../LICENSE.
// nifly is a separate GPL-3.0 dependency used only by this optional offline tool.
#include <NifFile.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace nifly;

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void BuildRibbon(NifFile& nif, bool perpendicular)
{
    constexpr int segments = 20;
    std::vector<Vector3> vertices, normals;
    std::vector<Vector2> uvs;
    std::vector<Color4> colors;
    std::vector<Triangle> triangles;
    for (int row = 0; row <= segments; ++row) {
        const float t = static_cast<float>(row) / segments;
        const float width = 0.1f * (1.0f - 0.88f * std::pow(t, 0.72f));
        // Increasing breakup towards the bottom, with an invisible final row.
        const float fade = t < 0.62f ? 0.78f : 0.78f * std::pow((1.0f - t) / 0.38f, 1.3f);
        for (int side = 0; side < 2; ++side) {
            const float horizontal = (side == 0 ? -0.5f : 0.5f) * width;
            vertices.emplace_back(perpendicular ? 0.0f : horizontal,
                                  perpendicular ? horizontal : 0.0f, -t);
            normals.emplace_back(perpendicular ? 1.0f : 0.0f,
                                 perpendicular ? 0.0f : 1.0f, 0.0f);
            uvs.emplace_back(static_cast<float>(side), t * 4.0f);
            colors.emplace_back(1.0f, 1.0f, 1.0f, fade);
        }
        if (row < segments) {
            const auto i = static_cast<uint16_t>(2 * row);
            triangles.emplace_back(i, i + 2, i + 1);
            triangles.emplace_back(i + 1, i + 2, i + 3);
        }
    }

    auto* shape = nif.CreateShapeFromData(perpendicular ? "RainRunoff_RibbonY" : "RainRunoff_RibbonX",
                                        &vertices, &triangles, &uvs, &normals);
    Require(shape != nullptr, "Could not create ribbon geometry");
    shape->SetVertexColors(true);
    nif.SetColorsForShape(shape, colors);
    auto shader = std::make_unique<BSEffectShaderProperty>();
    shader->name.get() = "RainRunoff_WaterEffect";
    shader->shaderFlags1 = SLSF1_ZBUFFER_TEST | SLSF1_VERTEX_ALPHA;
    shader->shaderFlags2 = SLSF2_DOUBLE_SIDED | SLSF2_VERTEX_COLORS | SLSF2_EFFECT_LIGHTING;
    // Texture clamp mode is packed with lighting influences in the upper byte.
    // Low byte 1 = clamp S / wrap T. Upper byte 255 enables ambient lighting.
    shader->textureClampMode = 1u | (255u << 8);
    shader->sourceTexture.get() = "textures\\RainRunoff\\runoff.dds";
    shader->baseColor = Color4(0.85f, 0.91f, 0.98f, 0.8f);
    shader->baseColorScale = 0.8f;
    shader->uvScale = Vector2(1.0f, 1.0f);
    shader->uvOffset = Vector2(0.0f, 0.0f);
    shape->ShaderPropertyRef()->index = nif.GetHeader().AddBlock(std::move(shader));

    auto alpha = std::make_unique<NiAlphaProperty>();
    // SRC_ALPHA / INV_SRC_ALPHA blending, no alpha-test, normal alpha sorting.
    alpha->flags = 1u | (6u << 1) | (7u << 5);
    alpha->threshold = 0;
    shape->AlphaPropertyRef()->index = nif.GetHeader().AddBlock(std::move(alpha));
}

static void Validate(const std::filesystem::path& file)
{
    NifFile nif;
    Require(nif.Load(file) == 0, "NIF readback failed");
    Require(nif.GetHeader().GetVersion().IsSSE(), "Expected Skyrim SE BS version 100");
    auto shapes = nif.GetShapes();
    Require(shapes.size() == 2, "Expected exactly two crossed ribbons");
    Require(nif.GetRootNode()->name.get() == "RainRunoff", "Unexpected root name");
    for (auto* shape : shapes) {
        auto* shader = dynamic_cast<BSEffectShaderProperty*>(nif.GetShader(shape));
        Require(shader != nullptr, "Expected BSEffectShaderProperty");
        Require((shader->shaderFlags1 & SLSF1_ZBUFFER_TEST) != 0, "Depth test missing");
        Require((shader->shaderFlags2 & SLSF2_ZBUFFER_WRITE) == 0, "Transparent stream must not write depth");
        Require((shader->shaderFlags2 & SLSF2_DOUBLE_SIDED) != 0, "Expected double-sided stream");
        Require((shader->textureClampMode & 0xffu) == 1, "Expected clamp-S/wrap-T");
        auto* alpha = nif.GetHeader().GetBlock(shape->AlphaPropertyRef());
        Require(alpha != nullptr && alpha->flags == 237, "Incorrect alpha blending flags");
        auto* vertices = nif.GetVertsForShape(shape);
        auto* colors = nif.GetColorsForShape(shape);
        std::vector<Triangle> triangles;
        Require(shape->GetTriangles(triangles) && triangles.size() == 40, "Unexpected ribbon triangle count");
        Require(vertices && vertices->size() == 42, "Unexpected ribbon vertex count");
        Require(colors && colors->size() == vertices->size(), "Vertex fade data missing");
        Require(std::abs(vertices->front().z) < 0.0001f, "Top must be at z=0");
        Require(std::abs(vertices->back().z + 1.0f) < 0.0001f, "Bottom must be at z=-1");
        Require(colors->back().a == 0.0f, "Bottom must fade to zero alpha");
        const auto delta = (*vertices)[1] - (*vertices)[0];
        Require(std::abs(delta.length() - 0.1f) < 0.0001f, "Expected normalized top width 0.1");
    }
    std::cout << "Validated " << file << ": SSE BS100, 2 crossed tapered ribbons, 84 vertices, 80 triangles\n";
}

int main(int argc, char** argv)
{
    try {
        if (argc == 3 && std::string(argv[1]) == "--validate") {
            Validate(argv[2]);
            return 0;
        }
        Require(argc == 2, "Usage: runoff_assets path/to/stream.nif | runoff_assets --validate stream.nif");
        const std::filesystem::path file(argv[1]);
        std::filesystem::create_directories(file.parent_path());
        NifFile nif;
        nif.Create(NiVersion(V20_2_0_7, 12, 100));
        nif.GetRootNode()->name.get() = "RainRunoff";
        nif.GetHeader().SetCreatorInfo("RainRunoff original procedural asset");
        BuildRibbon(nif, false);
        BuildRibbon(nif, true);
        Require(nif.Save(file) == 0, "Could not save NIF");
        Validate(file);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
