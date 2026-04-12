#include <cmath>
#include <algorithm>
#include "CubismRenderer_Raylib.hpp"
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "Model/CubismModel.hpp"
#include "Math/CubismMatrix44.hpp"
#include "CubismShader_Raylib.hpp"
#include "Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp"

using namespace Live2D::Cubism::Framework;
using Live2D::Cubism::Framework::CubismModel;

static Matrix ToRaylibMatrix(const csmFloat32 *m)
{
    Matrix out = {};
    out.m0 = m[0];
    out.m4 = m[4];
    out.m8 = m[8];
    out.m12 = m[12];
    out.m1 = m[1];
    out.m5 = m[5];
    out.m9 = m[9];
    out.m13 = m[13];
    out.m2 = m[2];
    out.m6 = m[6];
    out.m10 = m[10];
    out.m14 = m[14];
    out.m3 = m[3];
    out.m7 = m[7];
    out.m11 = m[11];
    out.m15 = m[15];
    return out;
}

std::array<CubismRenderer::CubismTextureColor, 4> CubismRenderer_Raylib::s_colorChannels = {
    CubismRenderer::CubismTextureColor(1, 0, 0, 0),
    CubismRenderer::CubismTextureColor(0, 1, 0, 0),
    CubismRenderer::CubismTextureColor(0, 0, 1, 0),
    CubismRenderer::CubismTextureColor(0, 0, 0, 1)};

void CubismRenderer::StaticRelease() {}

CubismRenderer *CubismRenderer::Create(csmUint32 width, csmUint32 height)
{
    return CSM_NEW CubismRenderer_Raylib(width, height);
}

CubismRenderer_Raylib::CubismRenderer_Raylib(csmUint32 width, csmUint32 height) : CubismRenderer(width, height), m_isExternalColorBuffer(false) {}

void CubismRenderer_Raylib::EnsureClippingContexts(CubismModel *model)
{
    if (m_clippingInitialized || model == nullptr)
    {
        return;
    }
    m_clippingInitialized = true;

    int maskedDrawableCount = 0;
    const auto drawableCount = model->GetDrawableCount();
    auto drawableMasks = model->GetDrawableMasks();
    auto drawableMaskCounts = model->GetDrawableMaskCounts();

    m_clippingContextsForDraw.reserve(drawableCount);

    for (csmInt32 i = 0; i < drawableCount; i++)
    {
        if (drawableMaskCounts[i] <= 0)
        {
            m_clippingContextsForDraw.emplace_back(nullptr);
            continue;
        }
        maskedDrawableCount++;

        RaylibL2D::CubismClippingContext *cc = nullptr;
        for (const auto clippingContext : m_clippingContextsForMask)
        {
            if (clippingContext->IsSameClipping(drawableMasks[i], drawableMaskCounts[i]))
            {
                cc = clippingContext;
                goto NEXT;
            }
        }

        cc = CSM_NEW RaylibL2D::CubismClippingContext(drawableMasks[i], drawableMaskCounts[i]);
        m_clippingContextsForMask.emplace_back(cc);

    NEXT:
        cc->drawables.emplace_back(i);
        m_clippingContextsForDraw.emplace_back(cc);
    }

    if (maskedDrawableCount > 0 && m_fbo == 0)
    {
        InitializeOffscreenFrameBuffer();
    }

}

void CubismRenderer_Raylib::Initialize(CubismModel *model)
{
    EnsureClippingContexts(model);
    m_sortedDrawables.assign(model->GetDrawableCount(), 0);
    CubismRenderer::Initialize(model);
}

void CubismRenderer_Raylib::SetFrameBuffer(int fbo) { m_oldFbo = fbo; }

CubismRenderer_Raylib::~CubismRenderer_Raylib()
{
    for (auto clippingContext : m_clippingContextsForMask)
        delete clippingContext;
    if (m_maskRenderTexture.id != 0)
    {
        UnloadRenderTexture(m_maskRenderTexture);
        m_maskRenderTexture = {};
        m_fbo = 0;
        m_colorBuffer = 0;
    }
    else if (m_colorBuffer && !m_isExternalColorBuffer)
    {
        rlUnloadTexture(m_colorBuffer);
        m_colorBuffer = 0;
    }
    if (m_fbo)
    {
        rlUnloadFramebuffer(m_fbo);
        m_fbo = 0;
    }
    for (auto p : m_textureIdMapping)
        rlUnloadTexture(p.second);
}

void expandRect(Rectangle &rect, float w, float h)
{
    rect.x -= w, rect.width += w * 2;
    rect.y -= h, rect.height += h * 2;
}

void CubismRenderer_Raylib::EnableOffscreenBuffer()
{
    rlDrawRenderBatchActive();
    rlViewport(0, 0, m_clippingMaskBufferSize.first, m_clippingMaskBufferSize.second);
    rlEnableFramebuffer(m_fbo);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    rlClearColor(255, 255, 255, 255);
    rlClearScreenBuffers();
}

void CubismRenderer_Raylib::DisableOffscreenBuffer()
{
    rlDrawRenderBatchActive();
    rlEnableFramebuffer(m_oldFbo);
    rlViewport(0, 0, rlGetFramebufferWidth(), rlGetFramebufferHeight());
}

void CubismRenderer_Raylib::DoDrawModel()
{
    auto *model = GetModel();
    EnsureClippingContexts(model);
    const csmInt32 drawableCount = model->GetDrawableCount();

    if (m_sortedDrawables.size() != (size_t)drawableCount)
        m_sortedDrawables.assign(drawableCount, 0);

    m_batch.BeginApplyBatch();
    rlDisableScissorTest();
    rlDisableDepthTest();
    rlEnableColorBlend();

    if (model->IsUsingMasking())
    {
        int activeClipCount = 0;
        for (auto cc : m_clippingContextsForMask)
        {
            cc->UpdateBounds(model);
            if (cc->active)
                activeClipCount++;
        }
        if (activeClipCount > 0)
        {
            const int channelCount = 4;
            auto cc_it = m_clippingContextsForMask.begin();
            const auto cc_end = m_clippingContextsForMask.end();
            int channelMod = activeClipCount % channelCount;
            for (int channelId = 0; channelId < channelCount; ++channelId)
            {
                int channelContains = activeClipCount / channelCount;
                if (channelMod)
                {
                    channelContains++;
                    channelMod--;
                }
                int colCount = std::max(1, (int)std::round(std::sqrt(channelContains)));
                int rowCount = channelContains / colCount + (channelContains % colCount != 0);
                float colSize = 1.0f / colCount;
                float rowSize = 1.0f / rowCount;
                for (int currentPos = 0; currentPos < channelContains; ++currentPos)
                {
                    for (; cc_it != cc_end && !(*cc_it)->active; ++cc_it)
                        ;
                    if (cc_it != cc_end)
                    {
                        auto cc = *cc_it;
                        cc_it++;
                        cc->layoutChannel = channelId;
                        cc->layoutRegion = {colSize * (currentPos % colCount), rowSize * (currentPos / colCount), colSize, rowSize};
                    }
                }
            }

            EnableOffscreenBuffer();
            for (auto cc : m_clippingContextsForMask)
            {
                if (!cc->active)
                {
                    continue;
                }
                Rectangle boundOnModel = cc->clipBound;
                expandRect(boundOnModel, 0.005f * boundOnModel.width, 0.005f * boundOnModel.height);
                if (boundOnModel.width <= 0.0f || boundOnModel.height <= 0.0f)
                {
                    continue;
                }
                CubismMatrix44 matrixForDraw;
                matrixForDraw.LoadIdentity();
                matrixForDraw.TranslateRelative(cc->layoutRegion.x, cc->layoutRegion.y);
                matrixForDraw.ScaleRelative(cc->layoutRegion.width / boundOnModel.width, cc->layoutRegion.height / boundOnModel.height);
                matrixForDraw.TranslateRelative(-boundOnModel.x, -boundOnModel.y);
                cc->drawMatrix = ToRaylibMatrix(matrixForDraw.GetArray());

                CubismMatrix44 matrixForMask;
                matrixForMask.LoadIdentity();
                matrixForMask.TranslateRelative(-1.0f, -1.0f);
                matrixForMask.ScaleRelative(2.0f, 2.0f);
                matrixForMask.MultiplyByMatrix(&matrixForDraw);
                cc->maskMatrix = ToRaylibMatrix(matrixForMask.GetArray());

                for (auto clip : cc->clippings)
                {
                    IsCulling(model->GetDrawableCulling(clip) != 0);
                    auto mColorVec = model->GetDrawableMultiplyColor(clip);
                    auto sColorVec = model->GetDrawableScreenColor(clip);
                    CubismRenderer::CubismTextureColor mColor = {mColorVec.X, mColorVec.Y, mColorVec.Z, mColorVec.W};
                    CubismRenderer::CubismTextureColor sColor = {sColorVec.X, sColorVec.Y, sColorVec.Z, sColorVec.W};

                    DrawMeshInternal1(model->GetDrawableTextureIndex(clip), model->GetDrawableOpacity(clip), CubismRenderer::CubismBlendMode_Normal, false, mColor, sColor, true, cc, clip);
                }
            }
            DisableOffscreenBuffer();
        }
    }

    const auto drawableRenderOrder = model->GetRenderOrders();
    for (int i = 0; i < drawableCount; ++i)
        m_sortedDrawables[drawableRenderOrder[i]] = i;

    for (const auto idx : m_sortedDrawables)
    {
        if (!model->GetDrawableDynamicFlagIsVisible(idx))
            continue;
        RaylibL2D::CubismClippingContext *cc = nullptr;
        if (idx >= 0 && idx < static_cast<int>(m_clippingContextsForDraw.size()))
        {
            cc = m_clippingContextsForDraw[idx];
        }
        IsCulling(model->GetDrawableCulling(idx) != 0);
        auto mColorVec = model->GetDrawableMultiplyColor(idx);
        auto sColorVec = model->GetDrawableScreenColor(idx);
        auto blendMode = model->GetDrawableBlendModeType(idx);
        CubismRenderer::CubismTextureColor mColor = {mColorVec.X, mColorVec.Y, mColorVec.Z, mColorVec.W};
        CubismRenderer::CubismTextureColor sColor = {sColorVec.X, sColorVec.Y, sColorVec.Z, sColorVec.W};

        DrawMeshInternal1(model->GetDrawableTextureIndex(idx), model->GetDrawableOpacity(idx), static_cast<CubismRenderer::CubismBlendMode>(blendMode.GetColorBlendType()), model->GetDrawableInvertedMask(idx), mColor, sColor, false, cc, idx);
    }
    m_batch.EndApplyBatch();
}

void CubismRenderer_Raylib::DoDrawModel1() { DoDrawModel(); }

void CubismRenderer_Raylib::DrawMeshInternal(csmInt32, csmInt32, const csmUint16 *, const csmFloat32 *, const csmFloat32 *, csmBool, bool, const RaylibL2D::CubismClippingContext *) {}

void CubismRenderer_Raylib::DrawMeshInternal1(csmInt32 textureNo, csmFloat32 opacity, CubismRenderer::CubismBlendMode colorBlendMode, csmBool invertedMask, const CubismRenderer::CubismTextureColor &multiplyColor, const CubismRenderer::CubismTextureColor &screenColor, bool drawingMask, const RaylibL2D::CubismClippingContext *cc, int index)
{
    const auto textureId = m_textureIdMapping[textureNo];
    if (textureId == 0)
        return;

    auto model = GetModel();
    const int shaderId = drawingMask ? CubismShaderNames_SetupMask : 1 + ((cc != nullptr) ? (invertedMask ? 2 : 1) : 0) + (IsPremultipliedAlpha() ? 3 : 0);
    const CubismShaderSet *shaderSet = nullptr;
    CubismRenderBufferSet *currentBuffer = nullptr;
    m_batch.CreateBuffer(shaderId, shaderSet, currentBuffer);

    rlUpdateVertexBuffer(currentBuffer->vertexBufferPositionId, model->GetDrawableVertices(index), sizeof(csmFloat32) * model->GetDrawableVertexCount(index) * 2, 0);
    rlUpdateVertexBuffer(currentBuffer->vertexBufferTexcoordId, model->GetDrawableVertexUvs(index), sizeof(csmFloat32) * model->GetDrawableVertexCount(index) * 2, 0);
    rlUpdateVertexBufferElements(currentBuffer->vertexBufferElementId, model->GetDrawableVertexIndices(index), sizeof(csmUint16) * model->GetDrawableVertexIndexCount(index), 0);
    currentBuffer->indexCount = model->GetDrawableVertexIndexCount(index);

    rlDisableBackfaceCulling();
    CubismRenderer::CubismTextureColor modelColorRGBA = GetModelColor();
    if (!drawingMask)
    {
        modelColorRGBA.A *= opacity;
        if (IsPremultipliedAlpha())
        {
            modelColorRGBA.R *= modelColorRGBA.A;
            modelColorRGBA.G *= modelColorRGBA.A;
            modelColorRGBA.B *= modelColorRGBA.A;
        }
    }

    if (drawingMask)
    {
        rlSetBlendFactorsSeparate(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD, GL_FUNC_ADD);
        rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
        rlEnableShader(shaderSet->shaderProgram);
        rlActiveTextureSlot(0);
        rlEnableTexture(textureId);
        const int sampler0 = 0;
        rlSetUniform(shaderSet->samplerTexture0, &sampler0, RL_SHADER_UNIFORM_INT, 1);
        rlSetUniform(shaderSet->unifromChannelFlag, &s_colorChannels[cc->layoutChannel].R, RL_SHADER_UNIFORM_VEC4, 1);
        rlSetUniformMatrix(shaderSet->uniformClipMatrix, cc->maskMatrix);
        float uniformBaseColor[] = {cc->layoutRegion.x * 2.0f - 1.0f, cc->layoutRegion.y * 2.0f - 1.0f, (cc->layoutRegion.x + cc->layoutRegion.width) * 2.0f - 1.0f, (cc->layoutRegion.y + cc->layoutRegion.height) * 2.0f - 1.0f};
        rlSetUniform(shaderSet->uniformBaseColor, uniformBaseColor, RL_SHADER_UNIFORM_VEC4, 1);
    }
    else
    {
        switch (colorBlendMode)
        {
        case CubismRenderer::CubismBlendMode_Normal:
            rlSetBlendFactorsSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD, GL_FUNC_ADD);
            break;
        case CubismRenderer::CubismBlendMode_Additive:
            rlSetBlendFactorsSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE, GL_FUNC_ADD, GL_FUNC_ADD);
            break;
        case CubismRenderer::CubismBlendMode_Multiplicative:
            rlSetBlendFactorsSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE, GL_FUNC_ADD, GL_FUNC_ADD);
            break;
        default:
            break;
        }
        rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
        rlEnableShader(shaderSet->shaderProgram);
        if (cc)
        {
            rlActiveTextureSlot(1);
            rlEnableTexture(m_colorBuffer);
            const int sampler1 = 1;
            rlSetUniform(shaderSet->samplerTexture1, &sampler1, RL_SHADER_UNIFORM_INT, 1);
            rlSetUniformMatrix(shaderSet->uniformClipMatrix, cc->drawMatrix);
            rlSetUniform(shaderSet->unifromChannelFlag, &s_colorChannels[cc->layoutChannel].R, RL_SHADER_UNIFORM_VEC4, 1);
        }
        rlActiveTextureSlot(0);
        rlEnableTexture(textureId);
        const int sampler0 = 0;
        rlSetUniform(shaderSet->samplerTexture0, &sampler0, RL_SHADER_UNIFORM_INT, 1);
        rlSetUniformMatrix(shaderSet->uniformMatrix, ToRaylibMatrix(GetMvpMatrix().GetArray()));
        rlSetUniform(shaderSet->uniformBaseColor, &modelColorRGBA.R, RL_SHADER_UNIFORM_VEC4, 1);
    }
    rlSetUniform(shaderSet->uniformMultiplyColor, &multiplyColor.R, RL_SHADER_UNIFORM_VEC4, 1);
    rlSetUniform(shaderSet->uniformScreenColor, &screenColor.R, RL_SHADER_UNIFORM_VEC4, 1);

    rlEnableVertexArray(currentBuffer->vertexArrayObjectId);
    rlDrawVertexArrayElements(0, model->GetDrawableVertexIndexCount(index), 0);
    rlDisableVertexArray();

}

void CubismRenderer_Raylib::InitializeOffscreenFrameBuffer(unsigned int externalColorBuffer)
{
    if (m_maskRenderTexture.id != 0)
    {
        UnloadRenderTexture(m_maskRenderTexture);
        m_maskRenderTexture = {};
        m_fbo = 0;
        m_colorBuffer = 0;
    }
    else if (m_colorBuffer && !m_isExternalColorBuffer)
        rlUnloadTexture(m_colorBuffer);
    if (m_fbo)
        rlUnloadFramebuffer(m_fbo);
    if (externalColorBuffer)
    {
        m_isExternalColorBuffer = true;
        m_colorBuffer = externalColorBuffer;
        m_fbo = rlLoadFramebuffer();
        rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, m_fbo);
        rlFramebufferAttach(m_fbo, m_colorBuffer, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, 0);
    }
    else
    {
        m_isExternalColorBuffer = false;
        m_maskRenderTexture = LoadRenderTexture((int)m_clippingMaskBufferSize.first, (int)m_clippingMaskBufferSize.second);
        SetTextureFilter(m_maskRenderTexture.texture, TEXTURE_FILTER_POINT);
        m_fbo = m_maskRenderTexture.id;
        m_colorBuffer = m_maskRenderTexture.texture.id;
        rlTextureParameters(m_colorBuffer, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_POINT);
        rlTextureParameters(m_colorBuffer, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_POINT);
        rlTextureParameters(m_colorBuffer, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
        rlTextureParameters(m_colorBuffer, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    }
}

void CubismRenderer_Raylib::LoadModelTexture(int cubismTextureId, const char *path)
{
    auto texture = LoadTexture(path);
    rlTextureParameters(texture.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(texture.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(texture.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(texture.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    m_textureIdMapping[cubismTextureId] = texture.id;
}

void CubismRenderer_Raylib::SaveProfile() {}
void CubismRenderer_Raylib::RestoreProfile() {}
void CubismRenderer_Raylib::BeforeDrawModelRenderTarget() {}
void CubismRenderer_Raylib::AfterDrawModelRenderTarget() {}
