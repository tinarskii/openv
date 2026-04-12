#pragma once

#include <vector>
#include "raylib.h"
#include "CubismFramework.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Model/CubismModel.hpp"

using Live2D::Cubism::Framework::csmInt32;
using Live2D::Cubism::Framework::csmVector;

namespace RaylibL2D {
struct CubismClippingContext
{
    CubismClippingContext(const int* clippingDrawableIndices, int clipCount);

    bool IsSameClipping(const int* drawableMasks, int drawableMaskCounts) const;

    void UpdateBounds(Live2D::Cubism::Framework::CubismModel* model);

    bool active = false;
    int layoutChannel = 0;
    std::vector<int> clippings;
    std::vector<int> drawables;
    Rectangle clipBound = { 0 };       // region contains all clipped drawables
    Rectangle layoutRegion = { 0 };    // region in channel
    Matrix maskMatrix;
    Matrix drawMatrix;
    csmVector<csmInt32>* _clippedDrawableIndexList;  ///< このマスクにクリップされるDrawableのリスト
};
}