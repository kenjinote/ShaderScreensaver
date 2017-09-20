#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define GLEW_STATIC

#pragma comment(lib, "scrnsavw")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "glew32s")

#include <windows.h>
#include <scrnsave.h>
#include <dwmapi.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <math.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include "resource.h"

WNDPROC DefaultVideoWndProc;
HDC hDC;
BOOL active;
GLuint program;
GLuint vao;
GLuint vbo;
WNDPROC EditWndProc;
float width, height;
const TCHAR szClassName[] = TEXT("Window");
const GLfloat position[][2] = { { -1.f, -1.f },{ 1.f, -1.f },{ 1.f, 1.f },{ -1.f, 1.f } };
const int vertices = sizeof position / sizeof position[0];
const GLchar vsrc[] = "in vec4 position;void main(void){gl_Position = position;}";

int GetArea(const LPRECT lpRect)
{
	return (lpRect->right - lpRect->left) * (lpRect->bottom - lpRect->top);
}

bool operator>(const RECT& left, const RECT& right)
{
	return GetArea((LPRECT)&left) > GetArea((LPRECT)&right);
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	MONITORINFOEX MonitorInfoEx;
	MonitorInfoEx.cbSize = sizeof(MonitorInfoEx);
	if (GetMonitorInfo(hMonitor, &MonitorInfoEx) != 0) {
		DEVMODE dm = { 0 };
		dm.dmSize = sizeof(DEVMODE);
		if (EnumDisplaySettings(MonitorInfoEx.szDevice, ENUM_CURRENT_SETTINGS, &dm) != 0) {
			RECT rect = {
				(LONG)dm.dmPosition.x,
				(LONG)dm.dmPosition.y,
				(LONG)(dm.dmPosition.x + dm.dmPelsWidth),
				(LONG)(dm.dmPosition.y + dm.dmPelsHeight)
			};
			((std::vector<RECT>*)dwData)->push_back(rect);
		}
	}
	return TRUE;
}

#define REG_KEY "Software\\ShaderScreensaver\\Setting"
class Setting {
	LPSTR m_lpszShaderCode;
public:
	Setting() : m_lpszShaderCode(0) {
	}
	~Setting() {
		GlobalFree(m_lpszShaderCode);
	}
	void Load() {
		GlobalFree(m_lpszShaderCode);
		HKEY hKey;
		DWORD dwPosition;
		if (ERROR_SUCCESS == RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, 0, 0, KEY_READ, 0, &hKey, &dwPosition)) {
			DWORD dwType = REG_SZ;
			DWORD dwByte = 0;
			if (ERROR_SUCCESS == RegQueryValueExA(hKey, "ShaderCode", NULL, &dwType, NULL, &dwByte)) {
				m_lpszShaderCode = (LPSTR)GlobalAlloc(0, dwByte);
				RegQueryValueExA(hKey, "ShaderCode", NULL, &dwType, (LPBYTE)m_lpszShaderCode, &dwByte);
			}
			RegCloseKey(hKey);
		}
	}
	void Save() {
		HKEY hKey;
		DWORD dwPosition;
		if (ERROR_SUCCESS == RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, 0, 0, KEY_WRITE, 0, &hKey, &dwPosition)) {
			RegSetValueExA(hKey, "ShaderCode", 0, REG_SZ, (CONST BYTE *)m_lpszShaderCode, lstrlenA(m_lpszShaderCode) + 1);
			RegCloseKey(hKey);
		}
	}
	LPSTR GetShaderCode() {
		if (!m_lpszShaderCode || lstrlenA(m_lpszShaderCode) == 0) {
			return
				"uniform float time;\r\n"
				"uniform float width;\r\n"
				"uniform float height;\r\n"
				"\r\n"
				"void main()\r\n"
				"{\r\n"
				"    vec2 p = vec2(\r\n"
				"        sin(time / 2.0) * (width / 2.0 - 100.0) + (width / 2.0),\r\n"
				"        cos(time / 3.0) * (height / 2.0 - 100.0) + (height / 2.0)\r\n"
				"    );\r\n"
				"    float c = 32.0 / length(gl_FragCoord - p);\r\n"
				"    gl_FragColor = vec4(c*c, c*c, c*c, 1.0);\r\n"
				"}";
		}
		return m_lpszShaderCode;
	}
	void SetShaderCode(LPCSTR lpszShaderCode) {
		GlobalFree(m_lpszShaderCode);
		const int nSize = lstrlenA(lpszShaderCode);
		m_lpszShaderCode = (LPSTR)GlobalAlloc(0, nSize + 1);
		lstrcpyA(m_lpszShaderCode, lpszShaderCode);
	}
};

inline GLint GetShaderInfoLog(GLuint shader)
{
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == 0) OutputDebugString(TEXT("Compile Error\n"));
	GLsizei bufSize;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &bufSize);
	if (bufSize > 1) {
		LPSTR infoLog = (LPSTR)GlobalAlloc(0, bufSize);
		GLsizei length;
		glGetShaderInfoLog(shader, bufSize, &length, infoLog);
		OutputDebugStringA(infoLog);
		GlobalFree(infoLog);
	}
	return status;
}

inline GLint GetProgramInfoLog(GLuint program)
{
	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == 0) OutputDebugString(TEXT("Link Error\n"));
	GLsizei bufSize;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufSize);
	if (bufSize > 1) {
		LPSTR infoLog = (LPSTR)GlobalAlloc(0, bufSize);
		GLsizei length;
		glGetProgramInfoLog(program, bufSize, &length, infoLog);
		OutputDebugStringA(infoLog);
		GlobalFree(infoLog);
	}
	return status;
}

inline GLuint CreateProgram(LPCSTR vsrc, LPCSTR fsrc)
{
	const GLuint vobj = glCreateShader(GL_VERTEX_SHADER);
	if (!vobj) return 0;
	glShaderSource(vobj, 1, &vsrc, 0);
	glCompileShader(vobj);
	if (GetShaderInfoLog(vobj) == 0) {
		glDeleteShader(vobj);
		return 0;
	}
	const GLuint fobj = glCreateShader(GL_FRAGMENT_SHADER);
	if (!fobj) {
		glDeleteShader(vobj);
		return 0;
	}
	glShaderSource(fobj, 1, &fsrc, 0);
	glCompileShader(fobj);
	if (GetShaderInfoLog(fobj) == 0) {
		glDeleteShader(vobj);
		glDeleteShader(fobj);
		return 0;
	}
	GLuint program = glCreateProgram();
	if (program) {
		glAttachShader(program, vobj);
		glAttachShader(program, fobj);
		glLinkProgram(program);
		if (GetProgramInfoLog(program) == 0) {
			glDetachShader(program, fobj);
			glDetachShader(program, vobj);
			glDeleteProgram(program);
			program = 0;
		}
	}
	glDeleteShader(vobj);
	glDeleteShader(fobj);
	if (program) {
		glUseProgram(program);
		glUniform1f(glGetUniformLocation(program, "width"), width);
		glUniform1f(glGetUniformLocation(program, "height"), height);
	}
	return program;
}

inline BOOL InitGL()
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 2 * vertices, position, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	return TRUE;
}

inline VOID DrawGLScene(GLvoid)
{
	glClear(GL_COLOR_BUFFER_BIT);
	glUniform1f(glGetUniformLocation(program, "time"), GetTickCount() / 1000.0f);
	glBindVertexArray(vao);
	glDrawArrays(GL_QUADS, 0, vertices);
	glBindVertexArray(0);
	glFlush();
}

LRESULT CALLBACK DrawShaderControlProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_PAINT) {
		if (program) {
			DrawGLScene();
			SwapBuffers(hDC);
			ValidateRect(hWnd, 0);
		}
		else {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			LPCTSTR lpszText = TEXT("シェーダーコードのコンパイルに失敗しました。");
			TextOut(hdc, 10, 10, lpszText, lstrlen(lpszText));
			EndPaint(hWnd, &ps);
		}
		return 0;
	}
	else if (msg == WM_MOUSEMOVE) {
		return SendMessage(GetParent(hWnd), msg, wParam, lParam);
	}
	else {
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
}

LRESULT WINAPI ScreenSaverProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static Setting setting;
	static HWND hDrawShaderControl;
	static std::vector<RECT> MonitorList;
	static std::vector<HTHUMBNAIL> ThumbnailList;
	static BOOL bPreviewMode;
	static PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR), 1,
		PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA,
		32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
	static GLuint PixelFormat;
	static HWND hEdit;
	static HFONT hFont;
	static HINSTANCE hRtLib;
	static BOOL bEditVisible = TRUE;
	static HGLRC hRC;
	switch (msg)
	{
	case WM_CREATE:
	{
		int n;
		LPTSTR* argv = CommandLineToArgvW(GetCommandLine(), &n);
		if (argv) {
			for (int i = 1; i < n; ++i) {
				if (lstrcmpi(argv[i], L"/P") == 0 || lstrcmpi(argv[i], L"/L") == 0) {
					bPreviewMode = TRUE;
					break;
				}
			}
			LocalFree(argv);
		}
	}
	if (!bPreviewMode) {
		EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&MonitorList);
		std::sort(MonitorList.begin(), MonitorList.end(), std::greater<RECT>());
		width = (float)(MonitorList[0].right - MonitorList[0].left);
		height = (float)(MonitorList[0].bottom - MonitorList[0].top);
	}else {
		RECT rect;
		GetClientRect(hWnd, &rect);
		width = (float)rect.right;
		height = (float)rect.bottom;
	}
	setting.Load();
	{
		TCHAR szClassName[] = TEXT("DrawShaderControl");
		const WNDCLASS wndclass = { 0, DrawShaderControlProc, 0, 0, ((LPCREATESTRUCT)lParam)->hInstance, 0, LoadCursor(0, IDC_ARROW), 0, 0, szClassName };
		RegisterClass(&wndclass);
		hDrawShaderControl = CreateWindow(szClassName, 0, (bPreviewMode ? WS_CHILD | WS_DISABLED : WS_POPUP) | WS_VISIBLE, 0, 0, (int)width, (int)height, hWnd, 0, ((LPCREATESTRUCT)lParam)->hInstance, 0);
	}
	if (!hDrawShaderControl)
		return -1;
	if (!bPreviewMode) {
		for (unsigned int i = 1; i < MonitorList.size(); ++i) {
			HTHUMBNAIL thumbnail;
			if (SUCCEEDED(DwmRegisterThumbnail(hWnd, hDrawShaderControl, &thumbnail))) {
				ThumbnailList.push_back(thumbnail);
			}
		}
	}
	if (!(hDC = GetDC(hDrawShaderControl)) ||
		!(PixelFormat = ChoosePixelFormat(hDC, &pfd)) ||
		!SetPixelFormat(hDC, PixelFormat, &pfd) ||
		!(hRC = wglCreateContext(hDC)) ||
		!wglMakeCurrent(hDC, hRC) ||
		glewInit() != GLEW_OK ||
		!InitGL()) return -1;
	program = CreateProgram(vsrc, setting.GetShaderCode());
	SetTimer(hWnd, 0x1234, 1, 0);
	break;
	case WM_TIMER:
		KillTimer(hWnd, 0x1234);
		InvalidateRect(hDrawShaderControl, 0, 0);
		SetTimer(hWnd, 0x1234, 1, 0);
		break;
	case WM_SIZE:
		if (!bPreviewMode) {
			MoveWindow(hDrawShaderControl, MonitorList[0].left, MonitorList[0].top, MonitorList[0].right - MonitorList[0].left, MonitorList[0].bottom - MonitorList[0].top, TRUE);
			for (unsigned int i = 1; i < MonitorList.size(); ++i) {
				RECT dest = MonitorList[i];
				ScreenToClient(hWnd, (LPPOINT)&dest.left);
				ScreenToClient(hWnd, (LPPOINT)&dest.right);
				DWM_THUMBNAIL_PROPERTIES dskThumbProps;
				dskThumbProps.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY;
				dskThumbProps.fSourceClientAreaOnly = FALSE;
				dskThumbProps.fVisible = TRUE;
				dskThumbProps.opacity = 255;
				dskThumbProps.rcDestination = dest;
				DwmUpdateThumbnailProperties(ThumbnailList[i - 1], &dskThumbProps);
			}
		}
		break;
	case WM_DESTROY:
		KillTimer(hWnd, 0x1234);
		glUseProgram(0);
		if (program) glDeleteProgram(program);
		if (vbo) glDeleteBuffers(1, &vbo);
		if (vao) glDeleteVertexArrays(1, &vao);
		if (hRC) {
			wglMakeCurrent(0, 0);
			wglDeleteContext(hRC);
		}
		if (hDC) ReleaseDC(hWnd, hDC);
		if (!bPreviewMode) {
			for (auto thumbnail : ThumbnailList) {
				DwmUnregisterThumbnail(thumbnail);
			}
		}
		PostQuitMessage(0);
		break;
	default:
		break;
	}
	return DefScreenSaverProc(hWnd, msg, wParam, lParam);
}

BOOL WINAPI ScreenSaverConfigureDialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static Setting setting;
	switch (msg) {
	case WM_INITDIALOG:
		setting.Load();
		SetDlgItemTextA(hWnd, IDC_EDIT1, setting.GetShaderCode());
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_EDIT1:
			if (HIWORD(wParam) == EN_CHANGE) {
				EnableWindow(GetDlgItem(hWnd, IDC_APPLY), TRUE);
			}
			return TRUE;
		case IDOK:
			SendMessage(hWnd, WM_COMMAND, IDC_APPLY, 0);
			EndDialog(hWnd, IDOK);
			return TRUE;
		case IDCANCEL:
			EndDialog(hWnd, IDCANCEL);
			return TRUE;
		case IDC_APPLY:
		{
			EnableWindow(GetDlgItem(hWnd, IDC_APPLY), FALSE);
			HWND hEdit = GetDlgItem(hWnd, IDC_EDIT1);
			const int nSize = GetWindowTextLengthA(hEdit);
			LPSTR lpszShaderCode = (LPSTR)GlobalAlloc(0, nSize + 1);
			GetWindowTextA(hEdit, lpszShaderCode, nSize + 1);
			setting.SetShaderCode(lpszShaderCode);
			GlobalFree(lpszShaderCode);
			setting.Save();
		}
		return TRUE;
		case IDC_PREVIEW:
		{
			Setting settingOld;
			settingOld.Load();
			SendMessage(hWnd, WM_COMMAND, IDC_APPLY, 0);
			TCHAR szModulePath[MAX_PATH];
			if (GetModuleFileName(0, szModulePath, _countof(szModulePath))) {
				SHELLEXECUTEINFO sei = { 0 };
				sei.cbSize = sizeof(SHELLEXECUTEINFO);
				sei.hwnd = hWnd;
				sei.nShow = SW_SHOWNORMAL;
				sei.fMask = SEE_MASK_NOCLOSEPROCESS;
				sei.lpFile = szModulePath;
				sei.lpParameters = TEXT("/s");
				if (!ShellExecuteEx(&sei) || (const int)sei.hInstApp <= 32) {
					settingOld.Save();
					return TRUE;
				}
				WaitForSingleObject(sei.hProcess, INFINITE);
				settingOld.Save();
			}
		}
		return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

BOOL WINAPI RegisterDialogClasses(HANDLE hInst)
{
	return TRUE;
}
