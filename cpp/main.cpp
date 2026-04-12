#include "raylib.h"
#include "raylib-live2d.h"
#include "SharedMemory.hpp"
#include <stdio.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/wait.h>
#include <signal.h>
#endif

struct Vec2 { float x; float y; };

struct MouthTuning
{
	float smileLift;
	float smileOpenReduction;
	float roundOpenBoost;
	float formGain;
	float widenPow;
	float roundPow;
	float vowelA_Base;
	float vowelA_RoundPenalty;
	float vowelO_Base;
	float vowelO_RoundGain;
	float vowelU_Base;
	float vowelU_RoundGain;
	float vowelI_Base;
	float vowelI_WideGain;
	float vowelE_Base;
	float vowelE_WideGain;
};

struct EyeTuning
{
	float openCurvePow;
	float headWeightX;
	float headWeightY;
	float mouseWeightX;
	float mouseWeightY;
	float headAngleXDiv;
	float headAngleYDiv;
	float eyeBallGainX;
	float eyeBallGainY;
	float gazeLimitXBase;
	float gazeLimitXOpen;
	float gazeLimitUpBase;
	float gazeLimitUpOpen;
	float gazeLimitDownBase;
	float gazeLimitDownOpen;
	float closedDampThreshold;
	float closedDampPow;
	float eyeBallFormClosedMax;
	float eyeFormClosedMax;
	float pupilOpacityMin;
};

struct RuntimeProfile
{
	std::string modelDir;
	std::string modelJson;
	std::unordered_map<int, std::string> expressionKeybinds;
};

struct BrowserEntry
{
	std::string name;
	std::string path;
	bool isDirectory;
	bool isModelFile;
};

struct TuningRegistry
{
	std::unordered_map<std::string, float*> floats;

	void Register(const std::string &key, float &value)
	{
		floats[key] = &value;
	}

	bool SetFloat(const std::string &key, float value)
	{
		const auto it = floats.find(key);
		if (it == floats.end())
		{
			return false;
		}
		*(it->second) = value;
		return true;
	}
};

static std::string Trim(const std::string &input)
{
	size_t start = 0;
	while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])))
	{
		++start;
	}
	size_t end = input.size();
	while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])))
	{
		--end;
	}
	return input.substr(start, end - start);
}

static std::string ToUpper(const std::string &input)
{
	std::string out = input;
	for (char &c : out)
	{
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	return out;
}

struct KeyName
{
	int key;
	const char *name;
};

static const KeyName kBindableKeys[] = {
	{KEY_A, "A"}, {KEY_B, "B"}, {KEY_C, "C"}, {KEY_D, "D"}, {KEY_E, "E"}, {KEY_F, "F"}, {KEY_G, "G"},
	{KEY_H, "H"}, {KEY_I, "I"}, {KEY_J, "J"}, {KEY_K, "K"}, {KEY_L, "L"}, {KEY_M, "M"}, {KEY_N, "N"},
	{KEY_O, "O"}, {KEY_P, "P"}, {KEY_Q, "Q"}, {KEY_R, "R"}, {KEY_S, "S"}, {KEY_T, "T"}, {KEY_U, "U"},
	{KEY_V, "V"}, {KEY_W, "W"}, {KEY_X, "X"}, {KEY_Y, "Y"}, {KEY_Z, "Z"},
	{KEY_ZERO, "0"}, {KEY_ONE, "1"}, {KEY_TWO, "2"}, {KEY_THREE, "3"}, {KEY_FOUR, "4"},
	{KEY_FIVE, "5"}, {KEY_SIX, "6"}, {KEY_SEVEN, "7"}, {KEY_EIGHT, "8"}, {KEY_NINE, "9"},
	{KEY_F1, "F1"}, {KEY_F2, "F2"}, {KEY_F3, "F3"}, {KEY_F4, "F4"}, {KEY_F5, "F5"}, {KEY_F6, "F6"},
	{KEY_F7, "F7"}, {KEY_F8, "F8"}, {KEY_F9, "F9"}, {KEY_F10, "F10"}, {KEY_F11, "F11"}, {KEY_F12, "F12"},
};

static int KeyFromName(const std::string &name)
{
	const std::string upper = ToUpper(name);
	for (const auto &k : kBindableKeys)
	{
		if (upper == k.name)
		{
			return k.key;
		}
	}
	return KEY_NULL;
}

static const char *NameFromKey(int key)
{
	for (const auto &k : kBindableKeys)
	{
		if (k.key == key)
		{
			return k.name;
		}
	}
	return "-";
}

static bool IsDirectoryPath(const std::string &path)
{
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0)
	{
		return false;
	}
	return S_ISDIR(st.st_mode);
}

static std::string ParentPath(const std::string &path)
{
	if (path.empty() || path == "/")
	{
		return path;
	}
	const size_t end = path.find_last_not_of('/');
	if (end == std::string::npos)
	{
		return "/";
	}
	const size_t slash = path.find_last_of('/', end);
	if (slash == std::string::npos)
	{
		return ".";
	}
	if (slash == 0)
	{
		return "/";
	}
	return path.substr(0, slash);
}

static bool IsAbsolutePath(const std::string &path)
{
	return !path.empty() && path[0] == '/';
}

static std::string JoinPath(const std::string &base, const std::string &name)
{
	if (base.empty())
	{
		return name;
	}
	if (base.back() == '/')
	{
		return base + name;
	}
	return base + "/" + name;
}

static bool FileExists(const std::string &path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool IsExecutableFile(const std::string &path)
{
#if defined(_WIN32)
	return FileExists(path);
#else
	return FileExists(path) && access(path.c_str(), X_OK) == 0;
#endif
}

static std::string ShellQuote(const std::string &input)
{
	std::string out = "'";
	for (char c : input)
	{
		if (c == '\'')
		{
			out += "'\"'\"'";
		}
		else
		{
			out.push_back(c);
		}
	}
	out += "'";
	return out;
}

static void SleepMs(int milliseconds)
{
#if defined(_WIN32)
	Sleep(static_cast<DWORD>(milliseconds));
#else
	usleep(static_cast<useconds_t>(milliseconds * 1000));
#endif
}

static bool EnsureDirectoryExists(const std::string &directory)
{
	if (directory.empty() || directory == ".")
	{
		return true;
	}
	if (IsDirectoryPath(directory))
	{
		return true;
	}

	const std::string parent = ParentPath(directory);
	if (parent != directory && !parent.empty() && !EnsureDirectoryExists(parent))
	{
		return false;
	}

	if (mkdir(directory.c_str(), 0755) == 0)
	{
		return true;
	}
	if (errno == EEXIST)
	{
		return IsDirectoryPath(directory);
	}
	return false;
}

static std::string CanonicalizePath(const std::string &path)
{
	char resolved[PATH_MAX] = {};
	if (realpath(path.c_str(), resolved) != nullptr)
	{
		return resolved;
	}
	return path;
}

static std::string ResolveModelDirectory(const std::string &modelDir, const std::string &profileFilePath)
{
	if (modelDir.empty() || IsAbsolutePath(modelDir))
	{
		return modelDir;
	}
	const std::string profileDir = ParentPath(profileFilePath);
	const std::string candidate = CanonicalizePath(JoinPath(profileDir, modelDir));
	if (IsDirectoryPath(candidate))
	{
		return candidate;
	}
	return modelDir;
}

static std::string GetUserProfilePath()
{
	const char *xdg = std::getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0] != '\0')
	{
		return JoinPath(JoinPath(xdg, "openv"), "profile.cfg");
	}
	const char *home = std::getenv("HOME");
	if (home && home[0] != '\0')
	{
		return JoinPath(JoinPath(JoinPath(home, ".config"), "openv"), "profile.cfg");
	}
	return "openv-profile.cfg";
}

static std::vector<BrowserEntry> ListBrowserEntries(const std::string &directory)
{
	std::vector<BrowserEntry> out;
	DIR *dir = opendir(directory.c_str());
	if (!dir)
	{
		return out;
	}
	struct dirent *entry = nullptr;
	while ((entry = readdir(dir)) != nullptr)
	{
		const std::string name = entry->d_name;
		if (name == "." || name == "..")
		{
			continue;
		}
		const std::string path = directory + "/" + name;
		const bool isDir = entry->d_type == DT_DIR || (entry->d_type == DT_UNKNOWN && IsDirectoryPath(path));
		const bool isModel = !isDir && name.size() > 12 && name.substr(name.size() - 12) == ".model3.json";
		if (isDir || isModel)
		{
			out.push_back({name, path, isDir, isModel});
		}
	}
	closedir(dir);
	std::sort(out.begin(), out.end(), [](const BrowserEntry &a, const BrowserEntry &b) {
		if (a.isDirectory != b.isDirectory)
		{
			return a.isDirectory > b.isDirectory;
		}
		return a.name < b.name;
	});
	return out;
}

static bool SplitModelPath(const std::string &fullPath, std::string &outDirWithSlash, std::string &outJsonFile)
{
	const size_t slash = fullPath.find_last_of('/');
	if (slash == std::string::npos || slash + 1 >= fullPath.size())
	{
		return false;
	}
	outDirWithSlash = fullPath.substr(0, slash + 1);
	outJsonFile = fullPath.substr(slash + 1);
	return true;
}

static bool TryParseFloat(const std::string &text, float &outValue)
{
	char *end = nullptr;
	const float parsed = std::strtof(text.c_str(), &end);
	if (end == text.c_str())
	{
		return false;
	}
	while (*end != '\0')
	{
		if (!std::isspace(static_cast<unsigned char>(*end)))
		{
			return false;
		}
		++end;
	}
	outValue = parsed;
	return true;
}

static float GetEnvFloatClamped(const char *name, float fallback, float minValue, float maxValue)
{
	const char *raw = std::getenv(name);
	if (!raw || raw[0] == '\0')
	{
		return fallback;
	}
	char *end = nullptr;
	const float parsed = std::strtof(raw, &end);
	if (end == raw)
	{
		return fallback;
	}
	return std::max(minValue, std::min(maxValue, parsed));
}

static MouthTuning LoadMouthTuning()
{
	MouthTuning t = {
		0.22f, // smileLift
		0.34f, // smileOpenReduction
		0.18f, // roundOpenBoost
		0.88f, // formGain
		1.25f, // widenPow
		1.15f, // roundPow
		0.72f, // vowelA_Base
		0.22f, // vowelA_RoundPenalty
		0.18f, // vowelO_Base
		0.56f, // vowelO_RoundGain
		0.12f, // vowelU_Base
		0.34f, // vowelU_RoundGain
		0.10f, // vowelI_Base
		0.48f, // vowelI_WideGain
		0.10f, // vowelE_Base
		0.38f  // vowelE_WideGain
	};

	t.smileLift = GetEnvFloatClamped("OPENV_MOUTH_SMILE_LIFT", t.smileLift, -1.0f, 1.0f);
	t.smileOpenReduction = GetEnvFloatClamped("OPENV_MOUTH_SMILE_OPEN_REDUCTION", t.smileOpenReduction, 0.0f, 1.0f);
	t.roundOpenBoost = GetEnvFloatClamped("OPENV_MOUTH_ROUND_OPEN_BOOST", t.roundOpenBoost, 0.0f, 1.0f);
	t.formGain = GetEnvFloatClamped("OPENV_MOUTH_FORM_GAIN", t.formGain, 0.0f, 2.0f);
	t.widenPow = GetEnvFloatClamped("OPENV_MOUTH_WIDEN_POW", t.widenPow, 0.2f, 3.0f);
	t.roundPow = GetEnvFloatClamped("OPENV_MOUTH_ROUND_POW", t.roundPow, 0.2f, 3.0f);
	t.vowelA_Base = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_A_BASE", t.vowelA_Base, 0.0f, 2.0f);
	t.vowelA_RoundPenalty = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_A_ROUND_PENALTY", t.vowelA_RoundPenalty, 0.0f, 2.0f);
	t.vowelO_Base = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_O_BASE", t.vowelO_Base, 0.0f, 2.0f);
	t.vowelO_RoundGain = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_O_ROUND_GAIN", t.vowelO_RoundGain, 0.0f, 2.0f);
	t.vowelU_Base = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_U_BASE", t.vowelU_Base, 0.0f, 2.0f);
	t.vowelU_RoundGain = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_U_ROUND_GAIN", t.vowelU_RoundGain, 0.0f, 2.0f);
	t.vowelI_Base = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_I_BASE", t.vowelI_Base, 0.0f, 2.0f);
	t.vowelI_WideGain = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_I_WIDE_GAIN", t.vowelI_WideGain, 0.0f, 2.0f);
	t.vowelE_Base = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_E_BASE", t.vowelE_Base, 0.0f, 2.0f);
	t.vowelE_WideGain = GetEnvFloatClamped("OPENV_MOUTH_VOWEL_E_WIDE_GAIN", t.vowelE_WideGain, 0.0f, 2.0f);
	return t;
}

static EyeTuning LoadEyeTuning()
{
	EyeTuning t = {
		1.30f, // openCurvePow
		1.00f, // headWeightX
		1.00f, // headWeightY
		0.00f, // mouseWeightX
		0.00f, // mouseWeightY
		4.2f,  // headAngleXDiv
		5.2f,  // headAngleYDiv
		0.82f, // eyeBallGainX
		0.82f, // eyeBallGainY
		0.08f, // gazeLimitXBase
		0.44f, // gazeLimitXOpen
		0.01f, // gazeLimitUpBase
		0.18f, // gazeLimitUpOpen
		0.05f, // gazeLimitDownBase
		0.24f, // gazeLimitDownOpen
		0.35f, // closedDampThreshold
		1.00f, // closedDampPow
		0.30f, // eyeBallFormClosedMax
		0.72f, // eyeFormClosedMax
		0.18f  // pupilOpacityMin
	};

	t.openCurvePow = GetEnvFloatClamped("OPENV_EYE_OPEN_CURVE_POW", t.openCurvePow, 0.2f, 3.0f);
	t.headWeightX = GetEnvFloatClamped("OPENV_EYE_HEAD_WEIGHT_X", t.headWeightX, 0.0f, 2.0f);
	t.headWeightY = GetEnvFloatClamped("OPENV_EYE_HEAD_WEIGHT_Y", t.headWeightY, 0.0f, 2.0f);
	t.mouseWeightX = GetEnvFloatClamped("OPENV_EYE_MOUSE_WEIGHT_X", t.mouseWeightX, 0.0f, 2.0f);
	t.mouseWeightY = GetEnvFloatClamped("OPENV_EYE_MOUSE_WEIGHT_Y", t.mouseWeightY, 0.0f, 2.0f);
	t.headAngleXDiv = GetEnvFloatClamped("OPENV_EYE_HEAD_ANGLE_X_DIV", t.headAngleXDiv, 0.2f, 20.0f);
	t.headAngleYDiv = GetEnvFloatClamped("OPENV_EYE_HEAD_ANGLE_Y_DIV", t.headAngleYDiv, 0.2f, 20.0f);
	t.eyeBallGainX = GetEnvFloatClamped("OPENV_EYE_BALL_GAIN_X", t.eyeBallGainX, 0.0f, 2.0f);
	t.eyeBallGainY = GetEnvFloatClamped("OPENV_EYE_BALL_GAIN_Y", t.eyeBallGainY, 0.0f, 2.0f);
	t.gazeLimitXBase = GetEnvFloatClamped("OPENV_EYE_GAZE_LIMIT_X_BASE", t.gazeLimitXBase, 0.0f, 1.0f);
	t.gazeLimitXOpen = GetEnvFloatClamped("OPENV_EYE_GAZE_LIMIT_X_OPEN", t.gazeLimitXOpen, 0.0f, 1.0f);
	t.gazeLimitUpBase = GetEnvFloatClamped("OPENV_EYE_GAZE_LIMIT_UP_BASE", t.gazeLimitUpBase, 0.0f, 1.0f);
	t.gazeLimitUpOpen = GetEnvFloatClamped("OPENV_EYE_GAZE_LIMIT_UP_OPEN", t.gazeLimitUpOpen, 0.0f, 1.0f);
	t.gazeLimitDownBase = GetEnvFloatClamped("OPENV_EYE_GAZE_LIMIT_DOWN_BASE", t.gazeLimitDownBase, 0.0f, 1.0f);
	t.gazeLimitDownOpen = GetEnvFloatClamped("OPENV_EYE_GAZE_LIMIT_DOWN_OPEN", t.gazeLimitDownOpen, 0.0f, 1.0f);
	t.closedDampThreshold = GetEnvFloatClamped("OPENV_EYE_CLOSED_DAMP_THRESHOLD", t.closedDampThreshold, 0.05f, 1.0f);
	t.closedDampPow = GetEnvFloatClamped("OPENV_EYE_CLOSED_DAMP_POW", t.closedDampPow, 0.2f, 3.0f);
	t.eyeBallFormClosedMax = GetEnvFloatClamped("OPENV_EYE_BALL_FORM_CLOSED_MAX", t.eyeBallFormClosedMax, 0.0f, 2.0f);
	t.eyeFormClosedMax = GetEnvFloatClamped("OPENV_EYE_FORM_CLOSED_MAX", t.eyeFormClosedMax, 0.0f, 2.0f);
	t.pupilOpacityMin = GetEnvFloatClamped("OPENV_EYE_PUPIL_OPACITY_MIN", t.pupilOpacityMin, 0.0f, 1.0f);
	return t;
}

static void RegisterMouthTuning(TuningRegistry &registry, MouthTuning &t)
{
	registry.Register("mouth.smileLift", t.smileLift);
	registry.Register("mouth.smileOpenReduction", t.smileOpenReduction);
	registry.Register("mouth.roundOpenBoost", t.roundOpenBoost);
	registry.Register("mouth.formGain", t.formGain);
	registry.Register("mouth.widenPow", t.widenPow);
	registry.Register("mouth.roundPow", t.roundPow);
	registry.Register("mouth.vowelA.base", t.vowelA_Base);
	registry.Register("mouth.vowelA.roundPenalty", t.vowelA_RoundPenalty);
	registry.Register("mouth.vowelO.base", t.vowelO_Base);
	registry.Register("mouth.vowelO.roundGain", t.vowelO_RoundGain);
	registry.Register("mouth.vowelU.base", t.vowelU_Base);
	registry.Register("mouth.vowelU.roundGain", t.vowelU_RoundGain);
	registry.Register("mouth.vowelI.base", t.vowelI_Base);
	registry.Register("mouth.vowelI.wideGain", t.vowelI_WideGain);
	registry.Register("mouth.vowelE.base", t.vowelE_Base);
	registry.Register("mouth.vowelE.wideGain", t.vowelE_WideGain);
}

static void RegisterEyeTuning(TuningRegistry &registry, EyeTuning &t)
{
	registry.Register("eye.openCurvePow", t.openCurvePow);
	registry.Register("eye.headWeightX", t.headWeightX);
	registry.Register("eye.headWeightY", t.headWeightY);
	registry.Register("eye.mouseWeightX", t.mouseWeightX);
	registry.Register("eye.mouseWeightY", t.mouseWeightY);
	registry.Register("eye.headAngleXDiv", t.headAngleXDiv);
	registry.Register("eye.headAngleYDiv", t.headAngleYDiv);
	registry.Register("eye.eyeBallGainX", t.eyeBallGainX);
	registry.Register("eye.eyeBallGainY", t.eyeBallGainY);
	registry.Register("eye.gazeLimitXBase", t.gazeLimitXBase);
	registry.Register("eye.gazeLimitXOpen", t.gazeLimitXOpen);
	registry.Register("eye.gazeLimitUpBase", t.gazeLimitUpBase);
	registry.Register("eye.gazeLimitUpOpen", t.gazeLimitUpOpen);
	registry.Register("eye.gazeLimitDownBase", t.gazeLimitDownBase);
	registry.Register("eye.gazeLimitDownOpen", t.gazeLimitDownOpen);
	registry.Register("eye.closedDampThreshold", t.closedDampThreshold);
	registry.Register("eye.closedDampPow", t.closedDampPow);
	registry.Register("eye.eyeBallFormClosedMax", t.eyeBallFormClosedMax);
	registry.Register("eye.eyeFormClosedMax", t.eyeFormClosedMax);
	registry.Register("eye.pupilOpacityMin", t.pupilOpacityMin);
}

static bool LoadProfileFile(const std::string &path, RuntimeProfile &profile, TuningRegistry &registry)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		return false;
	}
	profile.expressionKeybinds.clear();

	std::string line;
	while (std::getline(file, line))
	{
		const std::string trimmed = Trim(line);
		if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
		{
			continue;
		}
		const size_t eqPos = trimmed.find('=');
		if (eqPos == std::string::npos)
		{
			continue;
		}
		const std::string key = Trim(trimmed.substr(0, eqPos));
		const std::string value = Trim(trimmed.substr(eqPos + 1));
		if (key == "model.dir")
		{
			profile.modelDir = value;
			continue;
		}
		if (key == "model.json")
		{
			profile.modelJson = value;
			continue;
		}
		if (key.rfind("expr.bind.", 0) == 0)
		{
			const std::string keyName = key.substr(std::string("expr.bind.").size());
			const int bindKey = KeyFromName(keyName);
			if (bindKey != KEY_NULL)
			{
				if (value.empty())
				{
					profile.expressionKeybinds.erase(bindKey);
				}
				else
				{
					profile.expressionKeybinds[bindKey] = value;
				}
			}
			continue;
		}
		float f = 0.0f;
		if (TryParseFloat(value, f))
		{
			registry.SetFloat(key, f);
		}
	}
	return true;
}

static bool SaveProfileFile(const std::string &path, const RuntimeProfile &profile, const MouthTuning &mouth, const EyeTuning &eye)
{
	const std::string dir = ParentPath(path);
	if (!dir.empty() && dir != "." && !EnsureDirectoryExists(dir))
	{
		return false;
	}

	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open())
	{
		return false;
	}
	file << "# OpenV runtime profile\n";
	file << "model.dir=" << profile.modelDir << "\n";
	file << "model.json=" << profile.modelJson << "\n";
	for (const auto &entry : profile.expressionKeybinds)
	{
		file << "expr.bind." << NameFromKey(entry.first) << "=" << entry.second << "\n";
	}
	file << "\n";
	file << "# Mouth tuning\n";
	file << "mouth.smileLift=" << mouth.smileLift << "\n";
	file << "mouth.smileOpenReduction=" << mouth.smileOpenReduction << "\n";
	file << "mouth.roundOpenBoost=" << mouth.roundOpenBoost << "\n";
	file << "mouth.formGain=" << mouth.formGain << "\n";
	file << "mouth.widenPow=" << mouth.widenPow << "\n";
	file << "mouth.roundPow=" << mouth.roundPow << "\n";
	file << "mouth.vowelA.base=" << mouth.vowelA_Base << "\n";
	file << "mouth.vowelA.roundPenalty=" << mouth.vowelA_RoundPenalty << "\n";
	file << "mouth.vowelO.base=" << mouth.vowelO_Base << "\n";
	file << "mouth.vowelO.roundGain=" << mouth.vowelO_RoundGain << "\n";
	file << "mouth.vowelU.base=" << mouth.vowelU_Base << "\n";
	file << "mouth.vowelU.roundGain=" << mouth.vowelU_RoundGain << "\n";
	file << "mouth.vowelI.base=" << mouth.vowelI_Base << "\n";
	file << "mouth.vowelI.wideGain=" << mouth.vowelI_WideGain << "\n";
	file << "mouth.vowelE.base=" << mouth.vowelE_Base << "\n";
	file << "mouth.vowelE.wideGain=" << mouth.vowelE_WideGain << "\n";
	file << "\n";
	file << "# Eye tuning\n";
	file << "eye.openCurvePow=" << eye.openCurvePow << "\n";
	file << "eye.headWeightX=" << eye.headWeightX << "\n";
	file << "eye.headWeightY=" << eye.headWeightY << "\n";
	file << "eye.mouseWeightX=" << eye.mouseWeightX << "\n";
	file << "eye.mouseWeightY=" << eye.mouseWeightY << "\n";
	file << "eye.headAngleXDiv=" << eye.headAngleXDiv << "\n";
	file << "eye.headAngleYDiv=" << eye.headAngleYDiv << "\n";
	file << "eye.eyeBallGainX=" << eye.eyeBallGainX << "\n";
	file << "eye.eyeBallGainY=" << eye.eyeBallGainY << "\n";
	file << "eye.gazeLimitXBase=" << eye.gazeLimitXBase << "\n";
	file << "eye.gazeLimitXOpen=" << eye.gazeLimitXOpen << "\n";
	file << "eye.gazeLimitUpBase=" << eye.gazeLimitUpBase << "\n";
	file << "eye.gazeLimitUpOpen=" << eye.gazeLimitUpOpen << "\n";
	file << "eye.gazeLimitDownBase=" << eye.gazeLimitDownBase << "\n";
	file << "eye.gazeLimitDownOpen=" << eye.gazeLimitDownOpen << "\n";
	file << "eye.closedDampThreshold=" << eye.closedDampThreshold << "\n";
	file << "eye.closedDampPow=" << eye.closedDampPow << "\n";
	file << "eye.eyeBallFormClosedMax=" << eye.eyeBallFormClosedMax << "\n";
	file << "eye.eyeFormClosedMax=" << eye.eyeFormClosedMax << "\n";
	file << "eye.pupilOpacityMin=" << eye.pupilOpacityMin << "\n";
	return true;
}

static bool ReadFaceParamsSnapshot(const FaceParams *shared, FaceParams &snapshot)
{
	if (!shared)
	{
		return false;
	}

	for (int attempt = 0; attempt < 3; ++attempt)
	{
		const uint32_t versionStart = shared->version;
		if ((versionStart & 1u) != 0u)
		{
			continue;
		}

		const FaceParams local = *shared;
		const uint32_t versionEnd = shared->version;
		if (versionStart == versionEnd && (versionEnd & 1u) == 0u)
		{
			snapshot = local;
			return true;
		}
	}

	return false;
}

static float SmoothToward(float current, float target, float dt, float tau)
{
	if (tau <= 0.0f)
	{
		return target;
	}
	const float alpha = 1.0f - std::exp(-dt / tau);
	return current + (target - current) * alpha;
}

static bool UiButton(Rectangle rect, const char *label, bool enabled = true)
{
	const Vector2 mouse = GetMousePosition();
	const bool hovered = enabled && CheckCollisionPointRec(mouse, rect);
	DrawRectangleRec(rect, hovered ? Color{72, 72, 80, 220} : Color{52, 52, 58, 220});
	DrawRectangleLinesEx(rect, 1.0f, hovered ? RAYWHITE : (enabled ? LIGHTGRAY : GRAY));
	DrawText(label, static_cast<int>(rect.x) + 8, static_cast<int>(rect.y) + 6, 14, RAYWHITE);
	return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static bool UiSlider(Rectangle rect, const char *label, float &value, float minValue, float maxValue, bool enabled = true)
{
	const Vector2 mouse = GetMousePosition();
	const bool hovered = enabled && CheckCollisionPointRec(mouse, rect);
	const float range = std::max(0.0001f, maxValue - minValue);
	if (hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
	{
		const float t = std::max(0.0f, std::min(1.0f, (mouse.x - rect.x) / std::max(1.0f, rect.width)));
		value = minValue + t * range;
	}

	DrawText(label, static_cast<int>(rect.x), static_cast<int>(rect.y) - 15, 12, LIGHTGRAY);
	DrawRectangleRec(rect, Color{36, 36, 40, 220});
	const float t = std::max(0.0f, std::min(1.0f, (value - minValue) / range));
	const Rectangle fill = {rect.x, rect.y, rect.width * t, rect.height};
	DrawRectangleRec(fill, Color{100, 180, 255, 220});
	DrawRectangleLinesEx(rect, 1.0f, hovered ? RAYWHITE : GRAY);
	char valueText[64];
	std::snprintf(valueText, sizeof(valueText), "%.3f", value);
	DrawText(valueText, static_cast<int>(rect.x + rect.width + 8), static_cast<int>(rect.y) - 2, 12, RAYWHITE);
	return hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
}

int main(int argc, char **argv)
{
	SetConfigFlags(FLAG_MSAA_4X_HINT);
	InitWindow(1024, 768, "Live2D Test");
	ChangeDirectory(GetApplicationDirectory());
	SetTargetFPS(60);
	l2dInit();
	FaceParams *faceParams = SharedMemory::openReader();
	const std::string appDir = GetApplicationDirectory();
	#if defined(_WIN32)
	PROCESS_INFORMATION facecapProcess = {};
	bool facecapRunning = false;
	#else
	pid_t facecapPid = -1;
	#endif
	auto stopFacecapProcess = [&]() {
		#if defined(_WIN32)
		if (!facecapRunning)
		{
			return;
		}
		TerminateProcess(facecapProcess.hProcess, 0);
		WaitForSingleObject(facecapProcess.hProcess, 1000);
		CloseHandle(facecapProcess.hThread);
		CloseHandle(facecapProcess.hProcess);
		facecapRunning = false;
		#else
		if (facecapPid <= 0)
		{
			return;
		}
		kill(facecapPid, SIGTERM);
		for (int i = 0; i < 20; ++i)
		{
			int status = 0;
			const pid_t r = waitpid(facecapPid, &status, WNOHANG);
			if (r == facecapPid)
			{
				facecapPid = -1;
				return;
			}
			usleep(50 * 1000);
		}
		kill(facecapPid, SIGKILL);
		waitpid(facecapPid, nullptr, 0);
		facecapPid = -1;
		#endif
	};
	auto startFacecapProcess = [&](const std::string &commandLine, const std::string &workingDirectory) {
		if (commandLine.empty())
		{
			return false;
		}
		#if defined(_WIN32)
		std::string cmdline = commandLine;
		STARTUPINFOA startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo = {};
		BOOL ok = CreateProcessA(
			nullptr,
			&cmdline[0],
			nullptr,
			nullptr,
			FALSE,
			0,
			nullptr,
			workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
			&startupInfo,
			&processInfo);
		if (!ok)
		{
			printf("Warning: failed to launch facecap process (winerr=%lu).\n", static_cast<unsigned long>(GetLastError()));
			return false;
		}
		for (int i = 0; i < 10; ++i)
		{
			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 50);
			if (waitResult == WAIT_OBJECT_0)
			{
				DWORD exitCode = 0;
				GetExitCodeProcess(processInfo.hProcess, &exitCode);
				printf("Warning: facecap failed to start (exit=%lu): %s\n", static_cast<unsigned long>(exitCode), commandLine.c_str());
				CloseHandle(processInfo.hThread);
				CloseHandle(processInfo.hProcess);
				return false;
			}
		}
		facecapProcess = processInfo;
		facecapRunning = true;
		printf("Facecap started (pid=%lu): %s\n", static_cast<unsigned long>(processInfo.dwProcessId), commandLine.c_str());
		return true;
		#else
		const pid_t pid = fork();
		if (pid < 0)
		{
			printf("Warning: failed to launch facecap process.\n");
			return false;
		}
		if (pid == 0)
		{
			if (!workingDirectory.empty())
			{
				chdir(workingDirectory.c_str());
			}
			execl("/bin/sh", "sh", "-lc", commandLine.c_str(), static_cast<char *>(nullptr));
			_exit(127);
		}
		for (int i = 0; i < 10; ++i)
		{
			int status = 0;
			const pid_t r = waitpid(pid, &status, WNOHANG);
			if (r == pid)
			{
				if (WIFEXITED(status))
				{
					printf("Warning: facecap failed to start (exit=%d): %s\n", WEXITSTATUS(status), commandLine.c_str());
				}
				else
				{
					printf("Warning: facecap terminated while starting: %s\n", commandLine.c_str());
				}
				return false;
			}
			SleepMs(50);
		}
		facecapPid = pid;
		printf("Facecap started (pid=%d): %s\n", static_cast<int>(facecapPid), commandLine.c_str());
		return true;
		#endif
	};
	auto discoverFacecapCommand = [&](const std::string &preferred, bool explicitCommand) {
		if (explicitCommand && !preferred.empty())
		{
			return preferred;
		}
		std::vector<std::string> candidates;
		#if defined(_WIN32)
		candidates.push_back(JoinPath(appDir, "openv-facecap.exe"));
		candidates.push_back(JoinPath(appDir, "../bin/openv-facecap.exe"));
		candidates.push_back("./openv-facecap.exe");
		candidates.push_back("openv-facecap.exe");
		#else
		candidates.push_back(JoinPath(appDir, "openv-facecap"));
		candidates.push_back(JoinPath(appDir, "../bin/openv-facecap"));
		candidates.push_back("./openv-facecap");
		candidates.push_back("openv-facecap");
		#endif
		for (const auto &candidate : candidates)
		{
			if (candidate.empty())
			{
				continue;
			}
			if (candidate.find('/') != std::string::npos)
			{
				const std::string resolved = CanonicalizePath(candidate);
				if (IsExecutableFile(resolved))
				{
					return resolved;
				}
				continue;
			}
			return candidate;
		}
		return std::string();
	};
	auto discoverFacecapDataRoot = [&]() {
		const std::vector<std::string> roots = {
			appDir,
			ParentPath(appDir),
			JoinPath(appDir, "..")
			#if !defined(_WIN32)
			,
			"/usr/share/openv",
			"/usr/local/share/openv"
			#endif
		};
		for (const auto &root : roots)
		{
			if (root.empty())
			{
				continue;
			}
			const std::string r = CanonicalizePath(root);
			if (FileExists(JoinPath(r, "model/model.onnx")) &&
				FileExists(JoinPath(r, "assets/meanFace.npy")) &&
				FileExists(JoinPath(r, "assets/shapeBasis.npy")) &&
				FileExists(JoinPath(r, "assets/blendShape.npy")))
			{
				return r;
			}
		}
		return appDir;
	};

	RuntimeProfile runtimeProfile = {"Resources/Haru/", "Haru.model3.json"};
	std::string profilePath;
	bool profilePathExplicit = false;
	bool autoLaunchFacecap = true;
	std::string facecapCmd = "openv-facecap";
	bool facecapCmdExplicit = false;
	const char *envFacecapCmd = std::getenv("OPENV_FACECAP_CMD");
	if (envFacecapCmd && envFacecapCmd[0] != '\0')
	{
		facecapCmd = envFacecapCmd;
		facecapCmdExplicit = true;
	}
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--no-facecap")
		{
			autoLaunchFacecap = false;
			continue;
		}
		if (arg == "--facecap-cmd" && i + 1 < argc)
		{
			facecapCmd = argv[++i];
			facecapCmdExplicit = true;
			autoLaunchFacecap = true;
			continue;
		}
		if (arg == "--profile" && i + 1 < argc)
		{
			profilePath = argv[++i];
			profilePathExplicit = true;
			continue;
		}
		if (arg == "--model-dir" && i + 1 < argc)
		{
			runtimeProfile.modelDir = argv[++i];
			continue;
		}
		if (arg == "--model-json" && i + 1 < argc)
		{
			runtimeProfile.modelJson = argv[++i];
			continue;
		}
		if (arg.size() > 0 && arg[0] != '-' && i + 1 < argc)
		{
			runtimeProfile.modelDir = argv[i];
			runtimeProfile.modelJson = argv[i + 1];
			break;
		}
	}
	if (autoLaunchFacecap)
	{
		const std::string launchCmd = discoverFacecapCommand(facecapCmd, facecapCmdExplicit);
		const std::string dataRoot = discoverFacecapDataRoot();
		std::string runCmd = launchCmd;
		#if !defined(_WIN32)
		if (!launchCmd.empty() && launchCmd.find('/') != std::string::npos)
		{
			runCmd = ShellQuote(launchCmd);
		}
		#endif
		if (!startFacecapProcess(runCmd, dataRoot))
		{
			printf("Warning: facecap auto-launch unavailable. Set OPENV_FACECAP_CMD or use --facecap-cmd.\n");
		}
	}

	MouthTuning mouthTuning = LoadMouthTuning();
	EyeTuning eyeTuning = LoadEyeTuning();
	const MouthTuning mouthDefaults = mouthTuning;
	const EyeTuning eyeDefaults = eyeTuning;
	TuningRegistry tuningRegistry;
	RegisterMouthTuning(tuningRegistry, mouthTuning);
	RegisterEyeTuning(tuningRegistry, eyeTuning);

	std::string profileLoadPath = profilePath;
	const std::string userProfile = GetUserProfilePath();
	if (profilePath.empty())
	{
		profilePath = userProfile; // Always save user changes to user config.
		const std::string appLocalProfile = "openv-profile.cfg";
		const std::string systemProfile = "/usr/share/openv/default-profile.cfg";
		const std::string systemLocalProfile = "/usr/local/share/openv/default-profile.cfg";
		if (FileExists(userProfile))
		{
			profileLoadPath = userProfile;
		}
		else if (FileExists(appLocalProfile))
		{
			profileLoadPath = appLocalProfile;
		}
		else if (FileExists(systemLocalProfile))
		{
			profileLoadPath = systemLocalProfile;
		}
		else if (FileExists(systemProfile))
		{
			profileLoadPath = systemProfile;
		}
		else
		{
			profileLoadPath.clear();
		}
	}

	if (!profileLoadPath.empty() && FileExists(profileLoadPath))
	{
		if (!LoadProfileFile(profileLoadPath, runtimeProfile, tuningRegistry))
		{
			printf("Warning: failed to load profile '%s'\n", profileLoadPath.c_str());
		}
		else
		{
			runtimeProfile.modelDir = ResolveModelDirectory(runtimeProfile.modelDir, profileLoadPath);
			printf("Profile loaded: %s\n", profileLoadPath.c_str());
		}
	}
	else if (profilePathExplicit)
	{
		printf("Warning: profile file does not exist: '%s'\n", profilePath.c_str());
	}

	void *model = l2dLoadModel(runtimeProfile.modelDir.c_str(), runtimeProfile.modelJson.c_str());
	if (!model)
	{
		printf("Failed to load model: dir='%s' json='%s'\n", runtimeProfile.modelDir.c_str(), runtimeProfile.modelJson.c_str());
		stopFacecapProcess();
		CloseWindow();
		return 1;
	}
	float modelHeight = 2.0f;
	l2dSetModelHeight(model, modelHeight);
	l2dSetModelX(model, 0.0f);
	l2dSetModelY(model, 0.0f);
	auto applyModelPose = [&](void *m) {
		l2dSetModelHeight(m, modelHeight);
		l2dSetModelX(m, 0.0f);
		l2dSetModelY(m, 0.0f);
	};
	float viewZoom = 1.0f;
	Vector2 viewOffset = {0.0f, 0.0f};
	bool isPanning = false;
	Vector2 panPrev = {0.0f, 0.0f};
	int sceneScale = 1;
	int sceneWidth = 0;
	int sceneHeight = 0;
	RenderTexture2D sceneTarget = {};

	const void *paramAngleX = l2dGetParameterId("ParamAngleX");
	const void *paramAngleY = l2dGetParameterId("ParamAngleY");
	const void *paramAngleZ = l2dGetParameterId("ParamAngleZ");
	const void *paramBodyAngleX = l2dGetParameterId("ParamBodyAngleX");
	const void *paramEyeBallX = l2dGetParameterId("ParamEyeBallX");
	const void *paramEyeBallY = l2dGetParameterId("ParamEyeBallY");
	const void *paramEyeBallForm = l2dGetParameterId("ParamEyeBallForm");
	const void *paramEyeForm = l2dGetParameterId("ParamEyeForm");
	const void *paramEyeX = l2dGetParameterId("ParamEyeX");
	const void *paramEyeY = l2dGetParameterId("ParamEyeY");
	const void *paramEyeLX = l2dGetParameterId("ParamEyeLX");
	const void *paramEyeLY = l2dGetParameterId("ParamEyeLY");
	const void *paramEyeRX = l2dGetParameterId("ParamEyeRX");
	const void *paramEyeRY = l2dGetParameterId("ParamEyeRY");
	const void *paramPupilX = l2dGetParameterId("ParamPupilX");
	const void *paramPupilY = l2dGetParameterId("ParamPupilY");
	const void *paramEyeLOpen = l2dGetParameterId("ParamEyeLOpen");
	const void *paramEyeROpen = l2dGetParameterId("ParamEyeROpen");
	const void *paramEyeOpen = l2dGetParameterId("ParamEyeOpen");
	const void *paramBrowLY = l2dGetParameterId("ParamBrowLY");
	const void *paramBrowRY = l2dGetParameterId("ParamBrowRY");
	const void *paramMouthOpenY = l2dGetParameterId("ParamMouthOpenY");
	const void *paramMouthForm = l2dGetParameterId("ParamMouthForm");
	const void *paramBodyAngleY = l2dGetParameterId("ParamBodyAngleY");
	const void *paramBodyAngleZ = l2dGetParameterId("ParamBodyAngleZ");
	const void *paramA = l2dGetParameterId("ParamA");
	const void *paramI = l2dGetParameterId("ParamI");
	const void *paramU = l2dGetParameterId("ParamU");
	const void *paramE = l2dGetParameterId("ParamE");
	const void *paramO = l2dGetParameterId("ParamO");
	const void *partEyeBall = l2dGetPartId("PartEyeBall");
	std::vector<std::string> expressionNames;
	auto rebuildExpressionList = [&]() {
		expressionNames.clear();
		const int expressionCount = l2dGetExpressionCount(model);
		for (int i = 0; i < expressionCount; ++i)
		{
			const char *exprName = l2dGetExpressionName(model, i);
			if (exprName && exprName[0] != '\0')
			{
				expressionNames.emplace_back(exprName);
			}
		}
	};
	rebuildExpressionList();
	std::string browserDir = runtimeProfile.modelDir.empty() ? std::string("Resources") : runtimeProfile.modelDir;
	if (!browserDir.empty() && browserDir.back() == '/')
	{
		browserDir.pop_back();
	}
	if (browserDir.empty())
	{
		browserDir = ".";
	}
	browserDir = CanonicalizePath(browserDir);
	std::vector<BrowserEntry> browserEntries = ListBrowserEntries(browserDir);
	bool showModelBrowser = false;
	int browserScrollOffset = 0;
	std::string selectedModelPath;
	int bindCaptureIndex = -1;
	int exprScrollOffset = 0;
	bool expressionOverrideActive = false;
	std::string activeExpressionName;
	auto isReservedEditorKey = [](int key) {
		return key == KEY_F1 || key == KEY_F5 || key == KEY_F6;
	};
	auto switchModel = [&](const std::string &dir, const std::string &json) {
		void *newModel = l2dLoadModel(dir.c_str(), json.c_str());
		if (!newModel)
		{
			printf("Failed to load model: dir='%s' json='%s'\n", dir.c_str(), json.c_str());
			return false;
		}
		applyModelPose(newModel);
		l2dDestroyModel(model);
		model = newModel;
		runtimeProfile.modelDir = dir;
		runtimeProfile.modelJson = json;
		browserDir = dir;
		if (!browserDir.empty() && browserDir.back() == '/')
		{
			browserDir.pop_back();
		}
		if (browserDir.empty())
		{
			browserDir = ".";
		}
		browserDir = CanonicalizePath(browserDir);
		browserEntries = ListBrowserEntries(browserDir);
		selectedModelPath.clear();
		rebuildExpressionList();
		bindCaptureIndex = -1;
		exprScrollOffset = 0;
		expressionOverrideActive = false;
		activeExpressionName.clear();
		printf("Model loaded: %s%s\n", dir.c_str(), json.c_str());
		return true;
	};
	auto playExpression = [&](const std::string &name) {
		l2dStopMotions(model);
		l2dSetExpression(model, name.c_str());
		expressionOverrideActive = true;
		activeExpressionName = name;
	};
	auto stopExpression = [&]() {
		l2dStopExpression(model);
		expressionOverrideActive = false;
		activeExpressionName.clear();
	};

	float targetAngleX = 0.0f, targetAngleY = 0.0f, targetAngleZ = 0.0f;
	float targetMouthOpen = 0.0f, targetMouthForm = 0.0f, targetEyeBallX = 0.0f, targetEyeBallY = 0.0f, targetEyeOpenL = 1.0f, targetEyeOpenR = 1.0f, targetBrowL = 0.0f, targetBrowR = 0.0f;
	float smoothAngleX = 0.0f, smoothAngleY = 0.0f, smoothAngleZ = 0.0f;
	float smoothMouthOpen = 0.0f, smoothMouthForm = 0.0f, smoothEyeBallX = 0.0f, smoothEyeBallY = 0.0f, smoothEyeOpenL = 1.0f, smoothEyeOpenR = 1.0f, smoothBrowL = 0.0f, smoothBrowR = 0.0f;
	uint32_t lastVersion = 0;
	bool hasFaceTarget = false;
	bool showEditor = true;

	while (!WindowShouldClose())
	{
		#if defined(_WIN32)
		if (facecapRunning)
		{
			const DWORD waitResult = WaitForSingleObject(facecapProcess.hProcess, 0);
			if (waitResult == WAIT_OBJECT_0)
			{
				printf("Warning: facecap process exited.\n");
				CloseHandle(facecapProcess.hThread);
				CloseHandle(facecapProcess.hProcess);
				facecapRunning = false;
			}
		}
		#else
		if (facecapPid > 0)
		{
			int facecapStatus = 0;
			const pid_t r = waitpid(facecapPid, &facecapStatus, WNOHANG);
			if (r == facecapPid)
			{
				printf("Warning: facecap process exited.\n");
				facecapPid = -1;
			}
		}
		#endif
		const float screenWidth = static_cast<float>(GetScreenWidth());
		const float screenHeight = static_cast<float>(GetScreenHeight());
		if (IsKeyPressed(KEY_F1))
		{
			showEditor = !showEditor;
		}
		const float panelTop = 12.0f;
		const float panelHeight = std::max(420.0f, screenHeight - 24.0f);
		const Rectangle editorRect = {12.0f, panelTop, 430.0f, panelHeight};
		const Rectangle exprRect = {450.0f, panelTop, 360.0f, panelHeight};
		const Rectangle browserRect = {90.0f, 70.0f, screenWidth - 180.0f, screenHeight - 140.0f};
		const bool mouseOnEditor = showEditor && CheckCollisionPointRec(GetMousePosition(), editorRect);
		const bool mouseOnExpr = showEditor && CheckCollisionPointRec(GetMousePosition(), exprRect);
		const bool mouseOnBrowser = showEditor && showModelBrowser && CheckCollisionPointRec(GetMousePosition(), browserRect);
		const bool mouseOnAnyUi = mouseOnEditor || mouseOnExpr || mouseOnBrowser;
		const bool leftClickThisFrame = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
		if (bindCaptureIndex >= 0)
		{
			const int capturedKey = GetKeyPressed();
			if (capturedKey != 0 && bindCaptureIndex < static_cast<int>(expressionNames.size()))
			{
				if (!isReservedEditorKey(capturedKey))
				{
					runtimeProfile.expressionKeybinds[capturedKey] = expressionNames[bindCaptureIndex];
				}
				bindCaptureIndex = -1;
			}
			if (IsKeyPressed(KEY_ESCAPE))
			{
				bindCaptureIndex = -1;
			}
		}
		else
		{
			for (const auto &entry : runtimeProfile.expressionKeybinds)
			{
				if (IsKeyPressed(entry.first))
				{
					playExpression(entry.second);
				}
			}
		}
		auto reloadProfile = [&]() {
			if (profilePath.empty())
			{
				return;
			}
			RuntimeProfile reloadedProfile = runtimeProfile;
			if (LoadProfileFile(profilePath, reloadedProfile, tuningRegistry))
			{
				const bool modelChanged = (reloadedProfile.modelDir != runtimeProfile.modelDir) || (reloadedProfile.modelJson != runtimeProfile.modelJson);
				runtimeProfile = reloadedProfile;
				printf("Profile reloaded: %s\n", profilePath.c_str());
				if (modelChanged)
				{
					switchModel(runtimeProfile.modelDir, runtimeProfile.modelJson);
					browserDir = runtimeProfile.modelDir;
					if (!browserDir.empty() && browserDir.back() == '/')
					{
						browserDir.pop_back();
					}
					if (browserDir.empty())
					{
						browserDir = ".";
					}
					browserDir = CanonicalizePath(browserDir);
					browserEntries = ListBrowserEntries(browserDir);
					selectedModelPath.clear();
				}
			}
			else
			{
				printf("Failed to reload profile: %s\n", profilePath.c_str());
			}
		};
		auto saveProfile = [&]() {
			if (profilePath.empty())
			{
				return;
			}
			if (SaveProfileFile(profilePath, runtimeProfile, mouthTuning, eyeTuning))
			{
				printf("Profile saved: %s\n", profilePath.c_str());
			}
			else
			{
				printf("Failed to save profile: %s\n", profilePath.c_str());
			}
		};
		const int nextSceneScale = (viewZoom > 2.5f) ? 4 : (viewZoom > 1.25f ? 2 : 1);
		const int nextSceneWidth = std::max(1, static_cast<int>(screenWidth) * nextSceneScale);
		const int nextSceneHeight = std::max(1, static_cast<int>(screenHeight) * nextSceneScale);
		if (sceneTarget.id == 0 || nextSceneScale != sceneScale || nextSceneWidth != sceneWidth || nextSceneHeight != sceneHeight)
		{
			if (sceneTarget.id != 0)
			{
				UnloadRenderTexture(sceneTarget);
			}
			sceneScale = nextSceneScale;
			sceneWidth = nextSceneWidth;
			sceneHeight = nextSceneHeight;
			sceneTarget = LoadRenderTexture(sceneWidth, sceneHeight);
			SetTextureFilter(sceneTarget.texture, TEXTURE_FILTER_BILINEAR);
		}
		const float invZoom = 1.0f / viewZoom;
		const float modelMouseX = (GetMouseX() - viewOffset.x) * invZoom;
		const float modelMouseY = (GetMouseY() - viewOffset.y) * invZoom;

		if (!mouseOnAnyUi && leftClickThisFrame && l2dHitTest(model, "Head", modelMouseX, modelMouseY))
		{
			l2dSetMotion(model, "TapBody", 0, 2);
		}
		const float wheel = mouseOnAnyUi ? 0.0f : GetMouseWheelMove();
		if (wheel != 0.0f)
		{
			const float oldZoom = viewZoom;
			const float nextZoom = std::max(0.5f, std::min(5.0f, viewZoom + wheel * 0.15f));
			if (nextZoom != oldZoom)
			{
				const float worldX = (GetMouseX() - viewOffset.x) / oldZoom;
				const float worldY = (GetMouseY() - viewOffset.y) / oldZoom;
				viewZoom = nextZoom;
				viewOffset.x = GetMouseX() - worldX * viewZoom;
				viewOffset.y = GetMouseY() - worldY * viewZoom;
			}
		}
		if (!mouseOnAnyUi && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
		{
			isPanning = true;
			panPrev = {(float)GetMouseX(), (float)GetMouseY()};
		}
		if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
		{
			isPanning = false;
		}
		if (isPanning && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
		{
			const Vector2 panCur = {(float)GetMouseX(), (float)GetMouseY()};
			viewOffset.x += (panCur.x - panPrev.x);
			viewOffset.y += (panCur.y - panPrev.y);
			panPrev = panCur;
		}
		if (IsKeyPressed(KEY_F5))
		{
			reloadProfile();
		}
		if (IsKeyPressed(KEY_F6))
		{
			saveProfile();
		}

		l2dSetViewZoom(1.0f);
		l2dSetViewOffset(0.0f, 0.0f);
		l2dSetFrameBuffer(model, static_cast<int>(sceneTarget.id));

		BeginTextureMode(sceneTarget);
		ClearBackground(BLACK);
		l2dUpdate();
		l2dPreUpdateModel(model);

		{
			if (!faceParams)
			{
				faceParams = SharedMemory::openReader();
			}

			FaceParams snapshot;
			if (ReadFaceParamsSnapshot(faceParams, snapshot))
			{
				if (snapshot.version != lastVersion)
				{
					lastVersion = snapshot.version;
					targetAngleX = snapshot.angleX;
					targetAngleY = snapshot.angleY;
					targetAngleZ = snapshot.angleZ;
					targetMouthOpen = std::max(0.0f, std::min(1.0f, snapshot.mouthOpen));
					targetMouthForm = std::max(-1.0f, std::min(1.0f, snapshot.mouthForm));
					targetEyeBallX = std::max(-1.0f, std::min(1.0f, snapshot.eyeBallX));
					targetEyeBallY = std::max(-1.0f, std::min(1.0f, snapshot.eyeBallY));
					targetEyeOpenL = std::max(0.0f, std::min(1.0f, snapshot.eyeOpenL));
					targetEyeOpenR = std::max(0.0f, std::min(1.0f, snapshot.eyeOpenR));
					targetBrowL = std::max(-1.0f, std::min(1.0f, snapshot.browL));
					targetBrowR = std::max(-1.0f, std::min(1.0f, snapshot.browR));
					hasFaceTarget = true;
				}
			}
			if (hasFaceTarget)
			{
				const float dt = std::max(1.0f / 240.0f, std::min(0.05f, GetFrameTime()));
				smoothAngleX = SmoothToward(smoothAngleX, targetAngleX, dt, 0.08f);
				smoothAngleY = SmoothToward(smoothAngleY, targetAngleY, dt, 0.08f);
				smoothAngleZ = SmoothToward(smoothAngleZ, targetAngleZ, dt, 0.09f);
				smoothMouthOpen = SmoothToward(smoothMouthOpen, targetMouthOpen, dt, 0.06f);
				smoothMouthForm = SmoothToward(smoothMouthForm, targetMouthForm, dt, 0.07f);
				smoothEyeBallX = SmoothToward(smoothEyeBallX, targetEyeBallX, dt, 0.05f);
				smoothEyeBallY = SmoothToward(smoothEyeBallY, targetEyeBallY, dt, 0.05f);
				smoothEyeOpenL = SmoothToward(smoothEyeOpenL, targetEyeOpenL, dt, 0.04f);
				smoothEyeOpenR = SmoothToward(smoothEyeOpenR, targetEyeOpenR, dt, 0.04f);
				smoothBrowL = SmoothToward(smoothBrowL, targetBrowL, dt, 0.08f);
				smoothBrowR = SmoothToward(smoothBrowR, targetBrowR, dt, 0.08f);

				const float widenessRaw = std::max(0.0f, smoothMouthForm);
				const float roundnessRaw = std::max(0.0f, -smoothMouthForm);
				const float wideness = std::pow(widenessRaw, mouthTuning.widenPow);
				const float roundness = std::pow(roundnessRaw, mouthTuning.roundPow);
				const float mouthOpenNatural = std::max(0.0f, std::min(1.0f, smoothMouthOpen - mouthTuning.smileOpenReduction * wideness + mouthTuning.roundOpenBoost * roundness));
				const float mouthFormNatural = std::max(-1.0f, std::min(1.0f, smoothMouthForm * mouthTuning.formGain + mouthTuning.smileLift * wideness * (1.0f - mouthOpenNatural)));
				const float eyeOpenL = std::pow(std::max(0.0f, std::min(1.0f, smoothEyeOpenL)), eyeTuning.openCurvePow);
				const float eyeOpenR = std::pow(std::max(0.0f, std::min(1.0f, smoothEyeOpenR)), eyeTuning.openCurvePow);
				const float eyeOpen = (eyeOpenL + eyeOpenR) * 0.5f;
				const float trackedEyeX = std::max(-1.0f, std::min(1.0f, smoothAngleX / eyeTuning.headAngleXDiv + smoothEyeBallX * eyeTuning.eyeBallGainX));
				const float trackedEyeY = std::max(-1.0f, std::min(1.0f, smoothAngleY / eyeTuning.headAngleYDiv + smoothEyeBallY * eyeTuning.eyeBallGainY));
				const float eyeLookXRaw = trackedEyeX;
				const float eyeLookYRaw = trackedEyeY;
				const float gazeOpen = std::max(0.0f, std::min(1.0f, eyeOpen));
				const float gazeLimitX = eyeTuning.gazeLimitXBase + eyeTuning.gazeLimitXOpen * gazeOpen;
				const float gazeLimitUp = eyeTuning.gazeLimitUpBase + eyeTuning.gazeLimitUpOpen * gazeOpen;
				const float gazeLimitDown = eyeTuning.gazeLimitDownBase + eyeTuning.gazeLimitDownOpen * gazeOpen;
				float eyeLookX = std::max(-gazeLimitX, std::min(gazeLimitX, eyeLookXRaw));
				float eyeLookY = std::max(-gazeLimitUp, std::min(gazeLimitDown, eyeLookYRaw));
				if (gazeOpen < eyeTuning.closedDampThreshold)
				{
					const float t = std::pow(gazeOpen / eyeTuning.closedDampThreshold, eyeTuning.closedDampPow);
					eyeLookX *= t;
					eyeLookY *= t;
				}
				const float eyeBallForm = (1.0f - gazeOpen) * eyeTuning.eyeBallFormClosedMax;
				const float eyeForm = (1.0f - gazeOpen) * eyeTuning.eyeFormClosedMax;

				l2dSetParameter(model, paramAngleX, SetParameterType_Set, smoothAngleX, 1);
				l2dSetParameter(model, paramAngleY, SetParameterType_Set, smoothAngleY, 1);
				l2dSetParameter(model, paramAngleZ, SetParameterType_Set, smoothAngleZ, 1);
				l2dSetParameter(model, paramBodyAngleX, SetParameterType_Set, smoothAngleX * 0.3f, 1);
				l2dSetParameter(model, paramBodyAngleY, SetParameterType_Set, smoothAngleY * 0.15f, 1);
				l2dSetParameter(model, paramBodyAngleZ, SetParameterType_Set, -smoothAngleZ * 0.2f, 1);
				l2dSetParameter(model, paramEyeBallX, SetParameterType_Set, eyeLookX, 1);
				l2dSetParameter(model, paramEyeBallY, SetParameterType_Set, eyeLookY, 1);
				l2dSetParameter(model, paramEyeX, SetParameterType_Set, eyeLookX, 1);
				l2dSetParameter(model, paramEyeY, SetParameterType_Set, eyeLookY, 1);
				l2dSetParameter(model, paramEyeLX, SetParameterType_Set, eyeLookX, 1);
				l2dSetParameter(model, paramEyeLY, SetParameterType_Set, eyeLookY, 1);
				l2dSetParameter(model, paramEyeRX, SetParameterType_Set, eyeLookX, 1);
				l2dSetParameter(model, paramEyeRY, SetParameterType_Set, eyeLookY, 1);
				l2dSetParameter(model, paramPupilX, SetParameterType_Set, eyeLookX, 1);
				l2dSetParameter(model, paramPupilY, SetParameterType_Set, eyeLookY, 1);
				if (!expressionOverrideActive)
				{
					l2dSetParameter(model, paramEyeBallForm, SetParameterType_Set, eyeBallForm, 1);
					l2dSetParameter(model, paramEyeForm, SetParameterType_Set, eyeForm, 1);
					l2dSetParameter(model, paramEyeLOpen, SetParameterType_Set, eyeOpenL, 1);
					l2dSetParameter(model, paramEyeROpen, SetParameterType_Set, eyeOpenR, 1);
					l2dSetParameter(model, paramEyeOpen, SetParameterType_Set, eyeOpen, 1);
					l2dSetPartOpacity(model, partEyeBall, eyeTuning.pupilOpacityMin + (1.0f - eyeTuning.pupilOpacityMin) * gazeOpen);
					l2dSetParameter(model, paramBrowLY, SetParameterType_Set, smoothBrowL, 1);
					l2dSetParameter(model, paramBrowRY, SetParameterType_Set, smoothBrowR, 1);
					l2dSetParameter(model, paramMouthOpenY, SetParameterType_Set, mouthOpenNatural, 1);
					l2dSetParameter(model, paramMouthForm, SetParameterType_Set, mouthFormNatural, 1);

					// Mao-style vowel parameters
					l2dSetParameter(model, paramA, SetParameterType_Set, mouthOpenNatural * (mouthTuning.vowelA_Base - mouthTuning.vowelA_RoundPenalty * roundness), 1);
					l2dSetParameter(model, paramO, SetParameterType_Set, mouthOpenNatural * (mouthTuning.vowelO_Base + mouthTuning.vowelO_RoundGain * roundness), 1);
					l2dSetParameter(model, paramU, SetParameterType_Set, mouthOpenNatural * (mouthTuning.vowelU_Base + mouthTuning.vowelU_RoundGain * roundness), 1);
					l2dSetParameter(model, paramI, SetParameterType_Set, mouthOpenNatural * (mouthTuning.vowelI_Base + mouthTuning.vowelI_WideGain * wideness), 1);
					l2dSetParameter(model, paramE, SetParameterType_Set, mouthOpenNatural * (mouthTuning.vowelE_Base + mouthTuning.vowelE_WideGain * wideness), 1);
				}
			}
			else if (!expressionOverrideActive)
			{
				// Fallback when tracker data is unavailable: keep neutral gaze.
				l2dSetParameter(model, paramEyeBallX, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeBallY, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeBallForm, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeForm, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeX, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeY, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeLX, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeLY, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeRX, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramEyeRY, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramPupilX, SetParameterType_Set, 0.0f, 1);
				l2dSetParameter(model, paramPupilY, SetParameterType_Set, 0.0f, 1);
				l2dSetPartOpacity(model, partEyeBall, 1.0f);
			}
		}

		l2dUpdateModel(model);
		l2dDrawModel(model);
		EndTextureMode();

		l2dSetFrameBuffer(model, 0);

		BeginDrawing();
		ClearBackground(BLACK);
		const Rectangle src = {0.0f, 0.0f, static_cast<float>(sceneTarget.texture.width), -static_cast<float>(sceneTarget.texture.height)};
		const Rectangle dst = {viewOffset.x, viewOffset.y, screenWidth * viewZoom, screenHeight * viewZoom};
		DrawTexturePro(sceneTarget.texture, src, dst, {0.0f, 0.0f}, 0.0f, WHITE);
		if (showEditor)
		{
			const bool editorControlsEnabled = !showModelBrowser;
			DrawRectangleRec(editorRect, Color{20, 20, 24, 210});
			DrawRectangleLinesEx(editorRect, 1.0f, LIGHTGRAY);
			DrawText("OpenV Editor (F1 to toggle)", static_cast<int>(editorRect.x) + 10, static_cast<int>(editorRect.y) + 8, 16, RAYWHITE);
			DrawText(profilePath.empty() ? "Profile: <none>" : profilePath.c_str(), static_cast<int>(editorRect.x) + 10, static_cast<int>(editorRect.y) + 30, 12, LIGHTGRAY);

			const Rectangle reloadBtn = {editorRect.x + 10, editorRect.y + 50, 120, 24};
			const Rectangle saveBtn = {editorRect.x + 140, editorRect.y + 50, 120, 24};
			const Rectangle resetTuneBtn = {editorRect.x + 270, editorRect.y + 50, 150, 24};
			if (UiButton(reloadBtn, "Reload (F5)", editorControlsEnabled))
			{
				reloadProfile();
			}
			if (UiButton(saveBtn, "Save (F6)", editorControlsEnabled))
			{
				saveProfile();
			}
			if (UiButton(resetTuneBtn, "Reset tuning", editorControlsEnabled))
			{
				mouthTuning = mouthDefaults;
				eyeTuning = eyeDefaults;
			}
			const Rectangle resetViewBtn = {editorRect.x + 10, editorRect.y + 78, 120, 24};
			if (UiButton(resetViewBtn, "Reset view", editorControlsEnabled))
			{
				viewZoom = 1.0f;
				viewOffset = {0.0f, 0.0f};
			}

			DrawText("Model", static_cast<int>(editorRect.x) + 140, static_cast<int>(editorRect.y) + 84, 12, LIGHTGRAY);
			const Rectangle browseModelBtn = {editorRect.x + 180, editorRect.y + 78, 120, 24};
			if (UiButton(browseModelBtn, "Browse...", editorControlsEnabled))
			{
				showModelBrowser = true;
				browserEntries = ListBrowserEntries(browserDir);
				browserScrollOffset = 0;
				selectedModelPath.clear();
			}
			DrawText(runtimeProfile.modelJson.c_str(), static_cast<int>(editorRect.x) + 10, static_cast<int>(editorRect.y) + 106, 11, RAYWHITE);
			DrawText(runtimeProfile.modelDir.c_str(), static_cast<int>(editorRect.x) + 10, static_cast<int>(editorRect.y) + 120, 11, GRAY);

			struct SliderDef
			{
				const char *label;
				float *value;
				float minValue;
				float maxValue;
			};
			SliderDef sliders[] = {
				{"Mouth Smile Lift", &mouthTuning.smileLift, -1.0f, 1.0f},
				{"Mouth Smile Open Reduction", &mouthTuning.smileOpenReduction, 0.0f, 1.0f},
				{"Mouth Round Open Boost", &mouthTuning.roundOpenBoost, 0.0f, 1.0f},
				{"Mouth Form Gain", &mouthTuning.formGain, 0.0f, 2.0f},
				{"Mouth Widen Pow", &mouthTuning.widenPow, 0.2f, 3.0f},
				{"Eye Open Curve Pow", &eyeTuning.openCurvePow, 0.2f, 3.0f},
				{"Eye Head Weight X", &eyeTuning.headWeightX, 0.0f, 2.0f},
				{"Eye Mouse Weight X", &eyeTuning.mouseWeightX, 0.0f, 2.0f},
				{"Eye Head Weight Y", &eyeTuning.headWeightY, 0.0f, 2.0f},
				{"Eye Mouse Weight Y", &eyeTuning.mouseWeightY, 0.0f, 2.0f},
				{"Eye Gaze Limit X Open", &eyeTuning.gazeLimitXOpen, 0.0f, 1.0f},
				{"Eye Gaze Limit Up Open", &eyeTuning.gazeLimitUpOpen, 0.0f, 1.0f},
				{"Eye Gaze Limit Down Open", &eyeTuning.gazeLimitDownOpen, 0.0f, 1.0f},
				{"Eye Closed Damp Threshold", &eyeTuning.closedDampThreshold, 0.05f, 1.0f},
				{"Eye Pupil Opacity Min", &eyeTuning.pupilOpacityMin, 0.0f, 1.0f},
			};

			float y = editorRect.y + 140.0f;
			for (const auto &slider : sliders)
			{
				const Rectangle sliderRect = {editorRect.x + 10, y, 300.0f, 16.0f};
				UiSlider(sliderRect, slider.label, *slider.value, slider.minValue, slider.maxValue, editorControlsEnabled);
				y += 31.0f;
			}

			DrawRectangleRec(exprRect, Color{20, 20, 24, 210});
			DrawRectangleLinesEx(exprRect, 1.0f, LIGHTGRAY);
			DrawText("Expressions", static_cast<int>(exprRect.x) + 10, static_cast<int>(exprRect.y) + 8, 16, RAYWHITE);
			DrawText("Play + Bind Key", static_cast<int>(exprRect.x) + 10, static_cast<int>(exprRect.y) + 28, 12, LIGHTGRAY);
			const bool expressionPlayingNow = l2dIsExpressionPlaying(model) != 0;
			DrawText(expressionPlayingNow ? "State: playing" : (expressionOverrideActive ? "State: latched" : "State: idle"),
				static_cast<int>(exprRect.x) + 160, static_cast<int>(exprRect.y) + 28, 12, YELLOW);
			const Rectangle stopExprBtn = {exprRect.x + exprRect.width - 70.0f, exprRect.y + 8.0f, 60.0f, 20.0f};
			if (UiButton(stopExprBtn, "Stop", editorControlsEnabled))
			{
				stopExpression();
			}
			if (bindCaptureIndex >= 0 && bindCaptureIndex < static_cast<int>(expressionNames.size()))
			{
				std::string bindMessage = "Press key for " + expressionNames[bindCaptureIndex] + " (Esc cancel)";
				DrawText(bindMessage.c_str(), static_cast<int>(exprRect.x) + 10, static_cast<int>(exprRect.y) + 44, 11, YELLOW);
			}

			if (!showModelBrowser && mouseOnExpr)
			{
				const float wheelExpr = GetMouseWheelMove();
				if (wheelExpr != 0.0f)
				{
					exprScrollOffset -= static_cast<int>(wheelExpr);
					if (exprScrollOffset < 0)
					{
						exprScrollOffset = 0;
					}
				}
			}

			const int visibleRows = std::max(6, static_cast<int>((exprRect.height - 84.0f) / 24.0f));
			const int maxScroll = std::max(0, static_cast<int>(expressionNames.size()) - visibleRows);
			if (exprScrollOffset > maxScroll)
			{
				exprScrollOffset = maxScroll;
			}

			float ey = exprRect.y + 66.0f;
			for (int i = exprScrollOffset; i < static_cast<int>(expressionNames.size()) && i < exprScrollOffset + visibleRows; ++i)
			{
				const std::string &exprName = expressionNames[i];
				DrawText(exprName.c_str(), static_cast<int>(exprRect.x) + 10, static_cast<int>(ey), 12, RAYWHITE);

				const Rectangle playBtn = {exprRect.x + 200, ey - 2.0f, 60.0f, 20.0f};
				const Rectangle bindBtn = {exprRect.x + 265, ey - 2.0f, 60.0f, 20.0f};
				if (UiButton(playBtn, "Play", editorControlsEnabled))
				{
					playExpression(exprName);
				}
				if (UiButton(bindBtn, "Bind", editorControlsEnabled))
				{
					bindCaptureIndex = i;
				}

				const char *boundKeyName = "-";
				for (const auto &entry : runtimeProfile.expressionKeybinds)
				{
					if (entry.second == exprName)
					{
						boundKeyName = NameFromKey(entry.first);
						break;
					}
				}
				DrawText(boundKeyName, static_cast<int>(exprRect.x) + 330, static_cast<int>(ey), 12, LIGHTGRAY);
				ey += 24.0f;
			}

			if (showModelBrowser)
			{
				DrawRectangle(0, 0, static_cast<int>(screenWidth), static_cast<int>(screenHeight), Color{0, 0, 0, 120});
				DrawRectangleRec(browserRect, Color{24, 24, 28, 245});
				DrawRectangleLinesEx(browserRect, 1.0f, LIGHTGRAY);
				DrawText("Model File Explorer", static_cast<int>(browserRect.x) + 10, static_cast<int>(browserRect.y) + 8, 18, RAYWHITE);
				DrawText(browserDir.c_str(), static_cast<int>(browserRect.x) + 10, static_cast<int>(browserRect.y) + 32, 12, LIGHTGRAY);

				const Rectangle upBtn = {browserRect.x + 10, browserRect.y + 50, 60, 24};
				const Rectangle closeBtn = {browserRect.x + browserRect.width - 70, browserRect.y + 50, 60, 24};
				const Rectangle loadBtn = {browserRect.x + browserRect.width - 140, browserRect.y + 50, 60, 24};
				if (UiButton(upBtn, "Up"))
				{
					browserDir = ParentPath(browserDir);
					browserEntries = ListBrowserEntries(browserDir);
					browserScrollOffset = 0;
					selectedModelPath.clear();
				}
				if (UiButton(closeBtn, "Close"))
				{
					showModelBrowser = false;
				}
				if (UiButton(loadBtn, "Load") && !selectedModelPath.empty())
				{
					std::string modelDir;
					std::string modelJson;
					if (SplitModelPath(selectedModelPath, modelDir, modelJson))
					{
						switchModel(modelDir, modelJson);
						showModelBrowser = false;
					}
				}

				const Rectangle listRect = {browserRect.x + 10, browserRect.y + 82, browserRect.width - 20, browserRect.height - 120};
				DrawRectangleRec(listRect, Color{18, 18, 22, 255});
				DrawRectangleLinesEx(listRect, 1.0f, GRAY);
				if (mouseOnBrowser)
				{
					const float wheelBrowser = GetMouseWheelMove();
					if (wheelBrowser != 0.0f)
					{
						browserScrollOffset -= static_cast<int>(wheelBrowser);
						if (browserScrollOffset < 0)
						{
							browserScrollOffset = 0;
						}
					}
				}

				const int rowsVisible = static_cast<int>((listRect.height - 8.0f) / 24.0f);
				const int maxBrowserScroll = std::max(0, static_cast<int>(browserEntries.size()) - rowsVisible);
				if (browserScrollOffset > maxBrowserScroll)
				{
					browserScrollOffset = maxBrowserScroll;
				}
				float by = listRect.y + 4.0f;
				for (int i = browserScrollOffset; i < static_cast<int>(browserEntries.size()) && i < browserScrollOffset + rowsVisible; ++i)
				{
					const BrowserEntry &entry = browserEntries[i];
					const Rectangle rowRect = {listRect.x + 4.0f, by, listRect.width - 8.0f, 22.0f};
					const bool selected = selectedModelPath == entry.path;
					const Vector2 mousePos = GetMousePosition();
					const bool hovered = CheckCollisionPointRec(mousePos, rowRect);
					const Color rowColor = selected ? Color{62, 100, 150, 240} : (hovered ? Color{50, 50, 58, 240} : Color{28, 28, 33, 240});
					DrawRectangleRec(rowRect, rowColor);

					std::string label = entry.isDirectory ? ("[DIR] " + entry.name) : entry.name;
					DrawText(label.c_str(), static_cast<int>(rowRect.x) + 8, static_cast<int>(rowRect.y) + 4, 12, RAYWHITE);
					if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
					{
						if (entry.isDirectory)
						{
							browserDir = entry.path;
							browserEntries = ListBrowserEntries(browserDir);
							browserScrollOffset = 0;
							selectedModelPath.clear();
						}
						else if (entry.isModelFile)
						{
							selectedModelPath = entry.path;
						}
					}
					by += 24.0f;
				}

				DrawText(selectedModelPath.empty() ? "Selected: <none>" : selectedModelPath.c_str(),
					static_cast<int>(browserRect.x) + 10,
					static_cast<int>(browserRect.y + browserRect.height - 28),
					11,
					LIGHTGRAY);
			}
		}
		else
		{
			DrawRectangle(10, 10, 230, 28, Color{20, 20, 24, 180});
			DrawRectangleLines(10, 10, 230, 28, GRAY);
			DrawText("Press F1 to open editor", 18, 18, 12, RAYWHITE);
		}
		DrawFPS(100, 100);
		EndDrawing();
	}

	if (sceneTarget.id != 0)
	{
		UnloadRenderTexture(sceneTarget);
	}
	stopFacecapProcess();
	l2dDestroyModel(model);
	SharedMemory::close(faceParams);
	CloseWindow();
	return 0;
}
