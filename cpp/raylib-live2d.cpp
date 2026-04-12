#include "raylib-live2d.h"
#include "./l2d/LAppAllocator_Common.hpp"
#include "./l2d/LAppDefine.hpp"
#include "./l2d/LAppModel.hpp"
#include "./l2d/LAppPal.hpp"
#include "raylib.h"
#include "rlgl.h"
#include <Math/CubismMatrix44.hpp>
#include <Math/CubismModelMatrix.hpp>
#include <Math/CubismViewMatrix.hpp>
#include <Id/CubismId.hpp>
#include <Id/CubismIdManager.hpp>
#include <CubismFramework.hpp>
#include "./render/CubismRenderer_Raylib.hpp"
#include <string>
#include <GL/glew.h>

using namespace Csm;
using namespace LAppDefine;

Csm::CubismMatrix44 projection;
static float gViewOffsetX = 0.0f;
static float gViewOffsetY = 0.0f;
static float gViewZoom = 1.0f;
static bool gGlewReady = false;
static LAppAllocator_Common gCubismAllocator;
static CubismFramework::Option gCubismOption;

extern bool InitGlewForCubism(const char **errorText);

static csmByte* LoadFrameworkFile(const std::string filePath, csmSizeInt* outSize)
{
	int fileSize = 0;
	unsigned char* data = LoadFileData(filePath.c_str(), &fileSize);
	if (outSize)
	{
		*outSize = static_cast<csmSizeInt>(fileSize);
	}
	return reinterpret_cast<csmByte*>(data);
}

static void ReleaseFrameworkBytes(Csm::csmByte* byteData)
{
	UnloadFileData(reinterpret_cast<unsigned char*>(byteData));
}

extern "C"
{
	void l2dInit()
	{
		const char *glewError = nullptr;
		gGlewReady = InitGlewForCubism(&glewError);
		if (!gGlewReady)
		{
			printf("[APP] GLEW init failed: %s\n", glewError ? glewError : "unknown error");
			return;
		}

		gCubismOption.LogFunction = LAppPal::PrintMessage;
		gCubismOption.LoggingLevel = LAppDefine::CubismLoggingLevel;
		gCubismOption.LoadFileFunction = LoadFrameworkFile;
		gCubismOption.ReleaseBytesFunction = ReleaseFrameworkBytes;
		CubismFramework::StartUp(&gCubismAllocator, &gCubismOption);
		if (!CubismFramework::GetLoadFileFunction() || !CubismFramework::GetReleaseBytesFunction())
		{
			printf("[APP] Cubism file loader wiring failed.\n");
		}
		CubismFramework::Initialize();
		LAppPal::UpdateTime();
	}

	void *l2dLoadModel(const char *dir, const char *filename)
	{
		if (!gGlewReady)
		{
			printf("[APP] Cannot load model before successful GLEW initialization.\n");
			return nullptr;
		}

		auto model = new LAppModel();
		model->Initialize(dir, filename);
		if (model->GetModel() == nullptr)
		{
			delete model;
			return nullptr;
		}
		return model;
	}

	void l2dDestroyModel(void *model1)
	{
		if (!model1)
		{
			return;
		}
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		delete model;
	}

	void l2dSetModelWidth(void *model1, float v)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->GetModelMatrix()->SetWidth(v);
	}

	void l2dSetModelHeight(void *model1, float v)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->GetModelMatrix()->SetHeight(v);
	}

	void l2dSetModelX(void *model1, float v)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->GetModelMatrix()->SetX(v);
	}

	void l2dSetModelY(void *model1, float v)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->GetModelMatrix()->SetY(v);
	}

	void l2dSetViewOffset(float x, float y)
	{
		gViewOffsetX = x;
		gViewOffsetY = y;
	}

	void l2dSetViewZoom(float zoom)
	{
		gViewZoom = zoom;
	}

	void l2dUpdate(void)
	{
		rlDrawRenderBatchActive();
		LAppPal::UpdateTime();

		float width = (float)GetScreenWidth();
		float height = (float)GetScreenHeight();

		projection.LoadIdentity();
		if (width > height)
		{
			projection.Scale(height / width, 1.0f);
		}
		else
		{
			projection.Scale(1.0f, width / height);
		}
		projection.ScaleRelative(gViewZoom, gViewZoom);
		projection.TranslateRelative(gViewOffsetX, gViewOffsetY);
	}

	void l2dPreUpdateModel(void *model1)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->PreUpdate();
	}

	void l2dUpdateModel(void *model1)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->Update();
	}

	void l2dDrawModel(void *model1)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->Draw(projection);
		EndBlendMode();
	}

	void l2dSetFrameBuffer(void *model1, int fbo)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		if (auto *renderer = model->GetRenderer<CubismRenderer_Raylib>())
		{
			renderer->SetFrameBuffer(fbo);
		}
	}

	int l2dHitTest(void *model1, const char *name, float x, float y)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		return model->HitTest(name, x, GetScreenHeight() - y);
	}

	void l2dSetExpression(void *model1, const char *expid)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->SetExpression(expid);
	}

	void l2dStopExpression(void *model1)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->StopExpression();
	}

	int l2dIsExpressionPlaying(void *model1)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		return model->IsExpressionPlaying() ? 1 : 0;
	}

	int l2dGetExpressionCount(void *model1)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		return model->GetExpressionCount();
	}

	const char *l2dGetExpressionName(void *model1, int index)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		return model->GetExpressionName(index);
	}

	void l2dSetMotion(void *model1, const char *group, int no, int priority)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->StartMotion(group, no, priority);
	}

	void l2dStopMotions(void *model1)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->StopMotions();
	}

	const void *l2dGetParameterId(const char *name)
	{
		return CubismFramework::GetIdManager()->GetId(name);
	}

	const void *l2dGetPartId(const char *name)
	{
		return CubismFramework::GetIdManager()->GetId(name);
	}

	void l2dSetParameter(void *model1, const void *id, SetParameterType type, float value, float weight)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		switch (type)
		{
		case SetParameterType_Set:
			model->GetModel()->SetParameterValue(static_cast<const CubismId *>(id), value, weight);
			break;
		case SetParameterType_Add:
			model->GetModel()->AddParameterValue(static_cast<const CubismId *>(id), value, weight);
			break;
		case SetParameterType_Multiply:
			model->GetModel()->MultiplyParameterValue(static_cast<const CubismId *>(id), value, weight);
			break;
		}
	}

	float l2dGetParameter(void *model1, const void *id)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		return model->GetModel()->GetParameterValue(static_cast<const CubismId *>(id));
	}

	void l2dSetPartOpacity(void *model1, const void *id, float value)
	{
		LAppModel *model = reinterpret_cast<LAppModel *>(model1);
		model->GetModel()->SetPartOpacity(static_cast<const CubismId *>(id), value);
	}
}
