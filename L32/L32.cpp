// L32.cpp : 定义 DLL 的导出函数。
//

#include "pch.h"
#include "framework.h"
#include "L32.h"



#include <stdio.h>
#include <string>
#include <thread>
#include <time.h>
#include <sys/timeb.h>
#include <tchar.h>
#include <gl/gl.h>
#include "glext.h"

#include <easyhook.h>
#pragma comment(lib, "EasyHook64.lib")


#pragma comment(lib, "Opengl32.lib")


#define  OUTPUT_DEBUG_PRINTF(str)  OutputDebugPrintf(str)
void OutputDebugPrintf(const char* strOutputString, ...)
{
	//#ifdef _DEBUG
#define PUT_PUT_DEBUG_BUF_LEN   1024
	char strBuffer[PUT_PUT_DEBUG_BUF_LEN] = { 0 };
	va_list vlArgs;
	va_start(vlArgs, strOutputString);
	_vsnprintf_s(strBuffer, sizeof(strBuffer) - 1, strOutputString, vlArgs);  //_vsnprintf_s  _vsnprintf
	va_end(vlArgs);
	OutputDebugStringA(strBuffer);  //OutputDebugString
//#endif // DEBUG
}

PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glBufferData = NULL;
PFNGLGENBUFFERSPROC glGenBuffers = NULL;
PFNGLMAPBUFFERPROC glMapBuffer = NULL;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = NULL;


#define  BUF_SIZE 1920 * 1080 * 4 + 16 // 最大分辨率


typedef int(_stdcall* eglSwapBuffers)(void* display, void* surface);


typedef HWND(WINAPI* wapiCreateWindowExW)(
	DWORD     dwExStyle,
	LPCWSTR   lpClassName,
	LPCWSTR   lpWindowName,
	DWORD     dwStyle,
	int       X,
	int       Y,
	int       nWidth,
	int       nHeight,
	HWND      hWndParent,
	HMENU     hMenu,
	HINSTANCE hInstance,
	LPVOID    lpParam
	);

// 共享内存，获得图像存放到此处
HANDLE hShareMap;
void* hShareMapBuf;

HWND hwnd;

eglSwapBuffers peglSwapBuffers;

wapiCreateWindowExW pCreateWindowExW = nullptr;

/// <summary>
/// 读取屏幕数据
/// </summary>
GLuint              _pbo[2] = { 0 };
int                 _DMA = 0;
int                 _READ = 1;

int gWidth = 0; // 初始分辨率
int gHeight = 0;

unsigned char* image_read_buffer = nullptr;
unsigned char* image_read_buffer_cp = nullptr;




bool install_hook();

/// <summary>
/// 等待安装钩子
/// </summary>
void run_thread() {
	int n = 0;
	while (n < 100)
	{
		//Sleep(1500 * 60);
		if (install_hook()) {
			break;
		}
		Sleep(500);
		n++;
	}

}

void start_hook() {
	image_read_buffer_cp = (unsigned char*)malloc(BUF_SIZE);
	memset(image_read_buffer_cp, 0, BUF_SIZE);
	std::thread th(run_thread);
	th.detach();
}

void initGl() {
	glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
	glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
	glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
	glMapBuffer = (PFNGLMAPBUFFERPROC)wglGetProcAddress("glMapBuffer");
	glUnmapBuffer = (PFNGLUNMAPBUFFERPROC)wglGetProcAddress("glUnmapBuffer");

	glGenBuffers(1, _pbo);
	glBindBuffer(GL_ARRAY_BUFFER, _pbo[0]); //绑定 
	glBufferData(GL_ARRAY_BUFFER, BUF_SIZE, NULL, GL_STREAM_COPY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

}


int WINAPI myEglSwapBuffers(void* display, void* surface)
{
	GLint params[4];
	glGetIntegerv(GL_VIEWPORT, params);
	int x = params[0];
	int y = params[1];
	int width = params[2];
	int height = params[3];
	if (width * height > 1920 * 1080) {
		return peglSwapBuffers(display, surface);
	}
	//OutputDebugPrintf("DEBUG_INFO | ReadPix %d,%d,%d,%d", x, y, width, height);
	if (glBindBuffer == nullptr) {
		initGl();
		//OutputDebugPrintf("DEBUG_INFO | glinit  %x %x %x %x", glBindBuffer, glBufferData, glGenBuffers, glMapBuffer, glUnmapBuffer);
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo[0]);
	glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 0); //把屏幕数据读取到上面绑定的pbo中

	unsigned char* data = (unsigned char*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_WRITE);

	if (data) { // 写入共享内存
		struct timeb t1;
		ftime(&t1);
		time_t ttt = t1.millitm + t1.time * 1000;

		int total_size = 0;
		int fwidth = 0;
		int fheight = 0;

		//rgba to bgr
		int widthstep = width * 4;
		int _width = width;
		if (width % 2 != 0) // 需要补齐宽度为偶数，非偶数图像会出问题
		{
			_width += 1;
		}
		int _heidth = height;
		if (height % 2 != 0)
		{
			_heidth += 1;
		}
		int widthstep2 = _width * 3;
		for (int i = 0; i < height; i++) {
			for (int j = 0; j < width; j++) {
				unsigned char* p = image_read_buffer_cp + ((height - i - 1) * widthstep2) + j * 3;
				unsigned char* p2 = data + i * widthstep + j * 4;
				p[0] = p2[2];
				p[1] = p2[1];
				p[2] = p2[0];
			}
		}
		total_size = _width * _heidth * 3;
		fwidth = _width;
		fheight = _heidth;

		char* buffer = (char*)hShareMapBuf + 16;
		memcpy_s(buffer, BUF_SIZE - 16, image_read_buffer_cp, total_size);


		// 前16字节存放图像宽高和获取时间
		DWORD* tp = (DWORD*)hShareMapBuf;
		tp[0] = fwidth;
		tp[1] = fheight;
		*((time_t*)((char*)hShareMapBuf + sizeof(DWORD) * 2)) = ttt;
	}
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER); //取消映射
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	return peglSwapBuffers(display, surface);
}

HWND WINAPI myCreateWindowExW(DWORD     dwExStyle,
	LPCWSTR   lpClassName,
	LPCWSTR   lpWindowName,
	DWORD     dwStyle,
	int       X,
	int       Y,
	int       nWidth,
	int       nHeight,
	HWND      hWndParent,
	HMENU     hMenu,
	HINSTANCE hInstance,
	LPVOID    lpParam)
{
	hwnd = pCreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
	return hwnd;
}



//初始化
bool install_hook() {
	DWORD processId = GetCurrentProcessId();
	wchar_t readName[128];
	if (pCreateWindowExW == nullptr) {
		HMODULE huser32 = GetModuleHandle(L"User32.dll");
		if (huser32 != nullptr) {
			pCreateWindowExW = (wapiCreateWindowExW)GetProcAddress(huser32, "CreateWindowExW");
			//OutputDebugPrintf("DEBUG_INFO | pCreateWindowExW %x", pCreateWindowExW);


			HOOK_TRACE_INFO hHook_pCreateWindowExW = { NULL };
			NTSTATUS rest = LhInstallHook(pCreateWindowExW, myCreateWindowExW, NULL, &hHook_pCreateWindowExW);
			if (FAILED(rest))
			{
				//OutputDebugPrintf("DEBUG_INFO | LhInstallHook CreateWindowExW FAILED %x", rest);
				return false;
			}

			ULONG ACLEntries3[1] = { 0 };
			LhSetExclusiveACL(ACLEntries3, 1, &hHook_pCreateWindowExW);
		}
	}
	//OutputDebugPrintf("DEBUG_INFO | GLES_V2 Seccess %d", processId);
	
	// 不同的模拟器可能模块不一样
	HMODULE handleEGL = GetModuleHandle(L"libEGL_translator.dll");
	if (handleEGL == NULL) {
		//OutputDebugPrintf("DEBUG_INFO | libEGL_translator empty");
		handleEGL = GetModuleHandle(L"EGL.dll");
		if (handleEGL == NULL) {
			//OutputDebugPrintf("DEBUG_INFO | EGL empty");
			return false;
		}
	}
	peglSwapBuffers = (eglSwapBuffers)GetProcAddress(handleEGL, "eglSwapBuffers");
	//OutputDebugPrintf("DEBUG_INFO | eglSwapBuffers %x", peglSwapBuffers);


	TCHAR szName[64] = {};
	wsprintf(szName, TEXT("GBYS_FileMappingObject_%d"), processId);
	hShareMap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, BUF_SIZE, szName);
	hShareMapBuf = MapViewOfFile(hShareMap,   // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		BUF_SIZE);

	DWORD* tp = (DWORD*)hShareMapBuf;
	//OutputDebugPrintf("DEBUG_INFO | MapViewOfFile %x", hShareMapBuf);
	// 设置钩子
	HOOK_TRACE_INFO hHook = { NULL }; // keep track of our hook
	NTSTATUS result = LhInstallHook(peglSwapBuffers, myEglSwapBuffers, NULL, &hHook);
	if (FAILED(result))
	{
		//OutputDebugPrintf("DEBUG_INFO | LhInstallHook  eglSwapBuffers FAILED %x", result);
		return false;
	}

	// If the threadId in the ACL is set to 0, 
	// then internally EasyHook uses GetCurrentThreadId()
	ULONG ACLEntries[1] = { 0 };

	// Enable the hook for the provided threadIds
	LhSetExclusiveACL(ACLEntries, 1, &hHook);

	return true;
}

/// <summary>
///  定义导出函数，便于静态注入
/// </summary>
/// <returns></returns>
extern "C" _declspec(dllexport) int eProcess() {
	return 0;
}
/// <summary>
///  定义导出函数，便于静态注入
/// </summary>
/// <returns></returns>
extern "C" _declspec(dllexport) int ConsoleW() {
	return 0;
}


