#pragma once

#include <vector>
#include <map>
#include <array>
#include "Rendering/CubismRenderer.hpp"
#include "CubismFramework.hpp"
#include "Type/csmVector.hpp"
#include "Type/csmRectF.hpp"
#include "Math/CubismVector2.hpp"
#include "Type/csmMap.hpp"
#include "CubismClippingContext.hpp"
#include "CubismShader_Raylib.hpp"
#include "raylib.h"
#include <Live2DCubismCore.h>

using Live2D::Cubism::Framework::Rendering::CubismRenderer;
using Live2D::Cubism::Framework::CubismModel;
using Live2D::Cubism::Framework::csmInt32;
using Live2D::Cubism::Framework::csmUint32;
using Live2D::Cubism::Framework::csmUint16;
using Live2D::Cubism::Framework::csmFloat32;
using Live2D::Cubism::Framework::csmBool;

class CubismRenderer_Raylib : public CubismRenderer {
public:
    CubismRenderer_Raylib(csmUint32 width, csmUint32 height);
    virtual ~CubismRenderer_Raylib() override;
    virtual void Initialize(CubismModel* model) override;
    static CubismRenderer_Raylib* Create(Live2D::Cubism::Framework::csmUint32 width, Live2D::Cubism::Framework::csmUint32 height);
    void SetFrameBuffer(int fbo);
    virtual void SaveProfile() override;
    virtual void RestoreProfile() override;
    
    virtual void BeforeDrawModelRenderTarget() override;
    virtual void AfterDrawModelRenderTarget() override;
    void InitializeOffscreenFrameBuffer(unsigned int externalColorBuffer = 0);

    void LoadModelTexture(int cubismTextureId, const char* path);

    void DoDrawModel1();
protected:
    virtual void DoDrawModel() override;

private:
    void DrawMeshInternal(csmInt32 indexCount, csmInt32 vertexCount, const csmUint16* indexArray,
        const csmFloat32* vertexArray, const csmFloat32* uvArray, csmBool invertedMask,
        bool drawingMask, const RaylibL2D::CubismClippingContext* cc
    );

    void DrawMeshInternal1(csmInt32 textureNo, csmFloat32 opacity,
        CubismRenderer::CubismBlendMode colorBlendMode, csmBool invertedMask,
        const CubismRenderer::CubismTextureColor& multiplyColor,
        const CubismRenderer::CubismTextureColor& screenColor,
        bool drawingMask, const RaylibL2D::CubismClippingContext* cc,
        int index
    );

    void EnableOffscreenBuffer();

    void DisableOffscreenBuffer();
    void EnsureClippingContexts(CubismModel* model);

    bool m_isExternalColorBuffer;
    bool m_clippingInitialized = false;
    unsigned int m_oldFbo = 0;
    unsigned int m_fbo = 0;
    unsigned int m_colorBuffer = 0;
    RenderTexture2D m_maskRenderTexture = {};

    RenderBufferManager m_batch;

    std::pair<int, int> m_clippingMaskBufferSize = { 1024, 1024 };
    std::vector<RaylibL2D::CubismClippingContext*> m_clippingContextsForMask;
    std::vector<RaylibL2D::CubismClippingContext*> m_clippingContextsForDraw;
    std::vector<csmInt32> m_sortedDrawables;
    std::map<csmInt32, unsigned int> m_textureIdMapping;
    static std::array<CubismRenderer::CubismTextureColor, 4> s_colorChannels;
};
