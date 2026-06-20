// pch.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#define WINRT_LEAN_AND_MEAN 1

// Windows Header Files
#include <windows.h>
#undef GetCurrentTime
#include <unknwn.h>

// WinRT Header Files
#include <winrt/base.h>
#include <CppWinRTIncludes.h>
#include <winrt/Microsoft.ReactNative.h>

// C RunTime Header Files
#include <algorithm>
#include <malloc.h>
#include <memory.h>
#include <optional>
#include <string>
#include <stdlib.h>
#include <tchar.h>
#include <unordered_map>
#include <unordered_set>

// Reference additional headers your project requires here
