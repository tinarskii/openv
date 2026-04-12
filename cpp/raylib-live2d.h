#pragma once

#if defined(_WIN32)
  #define L2D_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define L2D_API __attribute__((visibility("default")))
#else
  #define L2D_API
#endif

#ifdef __cplusplus
extern "C" {
#endif
	typedef enum SetParameterType_t {
		SetParameterType_Set,
		SetParameterType_Add,
		SetParameterType_Multiply
	} SetParameterType;

	L2D_API void l2dInit();

	/// <summary>
	/// 加载Live2D模型
	/// </summary>
	/// <param name="dir">模型文件夹路径，以目录分隔符结尾</param>
	/// <param name="file">.model3.json文件名，不用包含目录</param>
	/// <returns>模型数据的指针</returns>
	L2D_API void* l2dLoadModel(const char* dir, const char* file);
	L2D_API void l2dDestroyModel(void* model);

	L2D_API void l2dSetModelWidth(void* model1, float v);

	L2D_API void l2dSetModelHeight(void* model1, float v);

	L2D_API void l2dSetModelX(void* model1, float v);

	L2D_API void l2dSetModelY(void* model1, float v);
	L2D_API void l2dSetViewOffset(float x, float y);
	L2D_API void l2dSetViewZoom(float zoom);

	L2D_API void l2dUpdate();

	L2D_API void l2dPreUpdateModel(void* model);

	L2D_API void l2dUpdateModel(void* model);

	L2D_API void l2dDrawModel(void* model);

	L2D_API void l2dSetFrameBuffer(void* model, int fbo);

	L2D_API int l2dHitTest(void* data, const char* name, float x, float y);

	L2D_API void l2dSetExpression(void* data, const char* expid);
	L2D_API void l2dStopExpression(void* data);
	L2D_API int l2dIsExpressionPlaying(void* data);
	L2D_API int l2dGetExpressionCount(void* data);
	L2D_API const char* l2dGetExpressionName(void* data, int index);

	L2D_API void l2dSetMotion(void* data, const char* group, int no, int priority);
	L2D_API void l2dStopMotions(void* data);

	L2D_API const void* l2dGetParameterId(const char* name);
	L2D_API const void* l2dGetPartId(const char* name);

	L2D_API void l2dSetParameter(void* data, const void* id, SetParameterType type, float value, float weight);

	L2D_API float l2dGetParameter(void* data, const void* id);
	L2D_API void l2dSetPartOpacity(void* data, const void* id, float value);
#ifdef __cplusplus
}
#endif
