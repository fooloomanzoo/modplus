//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2005 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <vector>
#include <algorithm>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <vd2/system/atomic.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>

#include <vd2/Riza/display.h>
#include "displaymgr.h"
#include "displaydrv.h"

#define VDDEBUG_DISP (void)sizeof printf
//#define VDDEBUG_DISP VDDEBUG

extern const char g_szVideoDisplayControlName[] = "phaeronVideoDisplay";

extern void VDMemcpyRect(void *dst, ptrdiff_t dststride, const void *src, ptrdiff_t srcstride, size_t w, size_t h);

extern IVDVideoDisplayMinidriver *VDCreateVideoDisplayMinidriverD3DFX();

///////////////////////////////////////////////////////////////////////////

namespace {
	bool VDIsTerminalServicesClient() {
		if ((sint32)(GetVersion() & 0x000000FF) >= 0x00000005) {
			return GetSystemMetrics(SM_REMOTESESSION) != 0;		// Requires Windows NT SP4 or later.
		}

		return false;	// Ignore Windows 95/98/98SE/ME/NT3/NT4.  (Broken on NT4 Terminal Server, but oh well.)
	}
}

///////////////////////////////////////////////////////////////////////////

VDVideoDisplayFrame::VDVideoDisplayFrame()
	: mRefCount(0)
{
}

VDVideoDisplayFrame::~VDVideoDisplayFrame() {
}

int VDVideoDisplayFrame::AddRef() {
	return ++mRefCount;
}

int VDVideoDisplayFrame::Release() {
	int rc = --mRefCount;

	if (!rc)
		delete this;

	return rc;
}

///////////////////////////////////////////////////////////////////////////

class VDVideoDisplayWindow : public IVDVideoDisplay, public IVDVideoDisplayMinidriverCallback, public VDVideoDisplayClient {
public:
	static ATOM Register();

protected:
	VDVideoDisplayWindow(HWND hwnd, const CREATESTRUCT& createInfo);
	~VDVideoDisplayWindow();

	void SetSourceMessage(const wchar_t *msg);
	void SetSourcePalette(const uint32 *palette, int count);
	bool SetSource(bool bAutoUpdate, const VDPixmap& src, void *pSharedObject, ptrdiff_t sharedOffset, bool bAllowConversion, bool bInterlaced);
	bool SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion, bool bInterlaced);
	void SetSourceSubrect(const vdrect32 *r);
	void SetSourceSolidColor(uint32 color);
	void SetFullScreen(bool fs);
	void PostBuffer(VDVideoDisplayFrame *);
	bool RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame);
	void FlushBuffers();
	void Update(int);
	void Destroy();
	void Reset();
	void Cache();
	void SetCallback(IVDVideoDisplayCallback *pcb);
	void SetAccelerationMode(AccelerationMode mode);
	FilterMode GetFilterMode();
	void SetFilterMode(FilterMode mode);
	float GetSyncDelta() const { return mSyncDelta; }

	void OnTick() {
		if (mpMiniDriver)
			mpMiniDriver->Poll();
	}

protected:
	void ReleaseActiveFrame();
	void RequestNextFrame();
	void DispatchNextFrame();

protected:
	static LRESULT CALLBACK StaticChildWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT ChildWndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnChildPaint();

	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnPaint();
	void SyncSetSourceMessage(const wchar_t *);
	bool SyncSetSource(bool bAutoUpdate, const VDVideoDisplaySourceInfo& params);
	void SyncReset();
	bool SyncInit(bool bAutoRefresh, bool bAllowNonpersistentSource);
	void SyncUpdate(int);
	void SyncCache();
	void SyncSetFilterMode(FilterMode mode);
	void SyncSetSolidColor(uint32 color);
	void OnDisplayChange();
	void OnForegroundChange(bool bForeground);
	void OnRealizePalette();
	bool InitMiniDriver();
	void ShutdownMiniDriver();
	void RequestUpdate();
	void VerifyDriverResult(bool result);

protected:
	enum {
		kReinitDisplayTimerId = 500
	};

	HWND		mhwnd;
	HWND		mhwndChild;
	HPALETTE	mhOldPalette;

	VDCriticalSection			mMutex;
	vdlist<VDVideoDisplayFrame>	mPendingFrames;
	vdlist<VDVideoDisplayFrame>	mIdleFrames;
	VDVideoDisplayFrame			*mpActiveFrame;
	VDVideoDisplayFrame			*mpLastFrame;
	VDVideoDisplaySourceInfo	mSource;

	IVDVideoDisplayMinidriver *mpMiniDriver;
	UINT	mReinitDisplayTimer;

	IVDVideoDisplayCallback		*mpCB;
	int		mInhibitRefresh;

	VDAtomicFloat	mSyncDelta;

	FilterMode	mFilterMode;
	AccelerationMode	mAccelMode;

	bool		mbIgnoreMouse;
	bool		mbUseSubrect;
	bool		mbFullScreen;
	vdrect32	mSourceSubrect;
	VDStringW	mMessage;

	uint32				mSolidColorBuffer;

	VDPixmapBuffer		mCachedImage;

	uint32	mSourcePalette[256];

	static ATOM				sChildWindowClass;

public:
	static bool		sbEnableDX;
	static bool		sbEnableDXOverlay;
	static bool		sbEnableD3D;
	static bool		sbEnableD3DFX;
	static bool		sbEnableOGL;
	static bool		sbEnableTS;
	static bool		sbEnableDebugInfo;
};

ATOM									VDVideoDisplayWindow::sChildWindowClass;
bool VDVideoDisplayWindow::sbEnableDX = true;
bool VDVideoDisplayWindow::sbEnableDXOverlay = true;
bool VDVideoDisplayWindow::sbEnableD3D;
bool VDVideoDisplayWindow::sbEnableD3DFX;
bool VDVideoDisplayWindow::sbEnableOGL;
bool VDVideoDisplayWindow::sbEnableTS;
bool VDVideoDisplayWindow::sbEnableDebugInfo;

///////////////////////////////////////////////////////////////////////////

void VDVideoDisplaySetDebugInfoEnabled(bool enable) {
	VDVideoDisplayWindow::sbEnableDebugInfo = enable;
}

void VDVideoDisplaySetFeatures(bool enableDirectX, bool enableDirectXOverlay, bool enableTermServ, bool enableOpenGL, bool enableDirect3D, bool enableDirect3DFX) {
	VDVideoDisplayWindow::sbEnableDX = enableDirectX;
	VDVideoDisplayWindow::sbEnableDXOverlay = enableDirectXOverlay;
	VDVideoDisplayWindow::sbEnableD3D = enableDirect3D;
	VDVideoDisplayWindow::sbEnableD3DFX = enableDirect3DFX;
	VDVideoDisplayWindow::sbEnableOGL = enableOpenGL;
	VDVideoDisplayWindow::sbEnableTS = enableTermServ;
}

///////////////////////////////////////////////////////////////////////////

ATOM VDVideoDisplayWindow::Register() {
	WNDCLASS wc;
	HMODULE hInst = VDGetLocalModuleHandleW32();

	if (!sChildWindowClass) {
		wc.style			= CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc		= StaticChildWndProc;
		wc.cbClsExtra		= 0;
		wc.cbWndExtra		= sizeof(VDVideoDisplayWindow *);
		wc.hInstance		= hInst;
		wc.hIcon			= 0;
		wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground	= 0;
		wc.lpszMenuName		= 0;
		wc.lpszClassName	= "phaeronVideoDisplayChild";

		sChildWindowClass = RegisterClass(&wc);
		if (!sChildWindowClass)
			return NULL;
	}

	wc.style			= CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc		= StaticWndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= sizeof(VDVideoDisplayWindow *);
	wc.hInstance		= hInst;
	wc.hIcon			= 0;
	wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground	= 0;
	wc.lpszMenuName		= 0;
	wc.lpszClassName	= g_szVideoDisplayControlName;

	return RegisterClass(&wc);
}

IVDVideoDisplay *VDGetIVideoDisplay(VDGUIHandle hwnd) {
	return static_cast<IVDVideoDisplay *>(reinterpret_cast<VDVideoDisplayWindow*>(GetWindowLongPtr((HWND)hwnd, 0)));
}

bool VDRegisterVideoDisplayControl() {
	return 0 != VDVideoDisplayWindow::Register();
}

///////////////////////////////////////////////////////////////////////////

VDVideoDisplayWindow::VDVideoDisplayWindow(HWND hwnd, const CREATESTRUCT& createInfo)
	: mhwnd(hwnd)
	, mhwndChild(NULL)
	, mhOldPalette(0)
	, mpMiniDriver(0)
	, mReinitDisplayTimer(0)
	, mpCB(0)
	, mInhibitRefresh(0)
	, mSyncDelta(0.0f)
	, mFilterMode(kFilterAnySuitable)
	, mAccelMode(kAccelOnlyInForeground)
	, mbIgnoreMouse(false)
	, mbUseSubrect(false)
	, mbFullScreen(false)
	, mpActiveFrame(NULL)
	, mpLastFrame(NULL)
{
	mSource.pixmap.data = 0;

	if (createInfo.hwndParent) {
		DWORD dwThreadId = GetWindowThreadProcessId(createInfo.hwndParent, NULL);
		if (dwThreadId == GetCurrentThreadId())
			mbIgnoreMouse = true;
	}

	VDVideoDisplayManager *vdm = (VDVideoDisplayManager *)createInfo.lpCreateParams;
	vdm->AddClient(this);
}

VDVideoDisplayWindow::~VDVideoDisplayWindow() {
	mpManager->RemoveClient(this);
}

///////////////////////////////////////////////////////////////////////////

#define MYWM_SETSOURCE		(WM_USER + 0x100)
#define MYWM_UPDATE			(WM_USER + 0x101)
#define MYWM_CACHE			(WM_USER + 0x102)
#define MYWM_RESET			(WM_USER + 0x103)
#define MYWM_SETSOURCEMSG	(WM_USER + 0x104)
#define MYWM_PROCESSNEXTFRAME	(WM_USER+0x105)
#define MYWM_DESTROY		(WM_USER + 0x106)
#define MYWM_SETFILTERMODE	(WM_USER + 0x107)
#define MYWM_SETSOLIDCOLOR	(WM_USER + 0x108)

void VDVideoDisplayWindow::SetSourceMessage(const wchar_t *msg) {
	SendMessage(mhwnd, MYWM_SETSOURCEMSG, 0, (LPARAM)msg);
}

void VDVideoDisplayWindow::SetSourcePalette(const uint32 *palette, int count) {
	memcpy(mSourcePalette, palette, 4*std::min<int>(count, 256));
}

bool VDVideoDisplayWindow::SetSource(bool bAutoUpdate, const VDPixmap& src, void *pObject, ptrdiff_t offset, bool bAllowConversion, bool bInterlaced) {
	// We do allow data to be NULL for set-without-load.
	if (src.data)
		VDAssertValidPixmap(src);

	VDVideoDisplaySourceInfo params;

	params.pixmap			= src;
	params.pSharedObject	= pObject;
	params.sharedOffset		= offset;
	params.bAllowConversion	= bAllowConversion;
	params.bPersistent		= pObject != 0;
	params.bInterlaced		= bInterlaced;

	const VDPixmapFormatInfo& info = VDPixmapGetInfo(src.format);
	params.bpp = info.qsize >> info.qhbits;
	params.bpr = (((src.w-1) >> info.qwbits)+1) * info.qsize;

	params.mpCB				= this;

	return 0 != SendMessage(mhwnd, MYWM_SETSOURCE, bAutoUpdate, (LPARAM)&params);
}

bool VDVideoDisplayWindow::SetSourcePersistent(bool bAutoUpdate, const VDPixmap& src, bool bAllowConversion, bool bInterlaced) {
	// We do allow data to be NULL for set-without-load.
	if (src.data)
		VDAssertValidPixmap(src);

	VDVideoDisplaySourceInfo params;

	params.pixmap			= src;
	params.pSharedObject	= NULL;
	params.sharedOffset		= 0;
	params.bAllowConversion	= bAllowConversion;
	params.bPersistent		= true;
	params.bInterlaced		= bInterlaced;

	const VDPixmapFormatInfo& info = VDPixmapGetInfo(src.format);
	params.bpp = info.qsize >> info.qhbits;
	params.bpr = (((src.w-1) >> info.qwbits)+1) * info.qsize;
	params.mpCB				= this;

	return 0 != SendMessage(mhwnd, MYWM_SETSOURCE, bAutoUpdate, (LPARAM)&params);
}

void VDVideoDisplayWindow::SetSourceSubrect(const vdrect32 *r) {
	if (r) {
		mbUseSubrect = true;
		mSourceSubrect = *r;
	} else
		mbUseSubrect = false;

	if (mpMiniDriver) {
		if (!mpMiniDriver->SetSubrect(r))
			SyncReset();
	}
}

void VDVideoDisplayWindow::SetSourceSolidColor(uint32 color) {
	SendMessage(mhwnd, MYWM_SETSOLIDCOLOR, 0, (LPARAM)color);
}

void VDVideoDisplayWindow::SetFullScreen(bool fs) {
	mbFullScreen = fs;
	if (mpMiniDriver)
		mpMiniDriver->SetFullScreen(fs);
	SetRequiresFullScreen(fs);
}

void VDVideoDisplayWindow::PostBuffer(VDVideoDisplayFrame *p) {
	p->AddRef();

	VDASSERT(p->mFlags & IVDVideoDisplay::kAllFields);

	bool wasIdle = false;
	vdsynchronized(mMutex) {
		if (!mpMiniDriver || !mpActiveFrame && mPendingFrames.empty())
			wasIdle = true;

		mPendingFrames.push_back(p);
	}

	if (wasIdle)
		PostMessage(mhwnd, MYWM_PROCESSNEXTFRAME, 0, 0);
}

bool VDVideoDisplayWindow::RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) {
	VDVideoDisplayFrame *p = NULL;
	vdsynchronized(mMutex) {
		if (allowFrameSkip && (!mPendingFrames.empty() && mPendingFrames.front() != mPendingFrames.back())) {
			p = mPendingFrames.back();
			mPendingFrames.pop_back();
		} else if (!mIdleFrames.empty()) {
			p = mIdleFrames.front();
			mIdleFrames.pop_front();
		}
	}

	if (!p)
		return false;

	*ppFrame = p;
	return true;
}

void VDVideoDisplayWindow::FlushBuffers() {
	vdlist<VDVideoDisplayFrame> frames;
	if (mpLastFrame) {
		frames.push_back(mpLastFrame);
		mpLastFrame = NULL;
	}

	// wait for any current frame to clear
	for(;;) {
		bool idle;
		vdsynchronized(mMutex) {
			// clear existing pending frames so the display doesn't start another render
			if (!mPendingFrames.empty())
				frames.splice(frames.end(), mIdleFrames);

			idle = !mpActiveFrame;
		}

		if (idle)
			break;

		::Sleep(1);
		OnTick();
	}

	vdsynchronized(mMutex) {
		frames.splice(frames.end(), mIdleFrames);
		frames.splice(frames.end(), mPendingFrames);
	}

	while(!frames.empty()) {
		VDVideoDisplayFrame *p = frames.back();
		frames.pop_back();

		p->Release();
	}
}

void VDVideoDisplayWindow::Update(int fieldmode) {
	SendMessage(mhwnd, MYWM_UPDATE, fieldmode, 0);
}

void VDVideoDisplayWindow::Cache() {
	SendMessage(mhwnd, MYWM_CACHE, 0, 0);
}

void VDVideoDisplayWindow::Destroy() {
	SendMessage(mhwnd, MYWM_DESTROY, 0, 0);
}

void VDVideoDisplayWindow::Reset() {
	SendMessage(mhwnd, MYWM_RESET, 0, 0);
}

void VDVideoDisplayWindow::SetCallback(IVDVideoDisplayCallback *pCB) {
	mpCB = pCB;
}

void VDVideoDisplayWindow::SetAccelerationMode(AccelerationMode mode) {
	mAccelMode = mode;
}

IVDVideoDisplay::FilterMode VDVideoDisplayWindow::GetFilterMode() {
	return mFilterMode;
}

void VDVideoDisplayWindow::SetFilterMode(FilterMode mode) {
	SendMessage(mhwnd, MYWM_SETFILTERMODE, 0, (LPARAM)mode);
}

void VDVideoDisplayWindow::ReleaseActiveFrame() {
	vdsynchronized(mMutex) {
		if (mpActiveFrame) {
			if (mpLastFrame) {
				mIdleFrames.push_front(mpLastFrame);
				mpLastFrame = NULL;
			}

			if (mpActiveFrame->mFlags & kAutoFlipFields) {
				mpActiveFrame->mFlags ^= 3;
				mpActiveFrame->mFlags &= ~(kAutoFlipFields | kFirstField);
				mPendingFrames.push_front(mpActiveFrame);
			} else
				mpLastFrame = mpActiveFrame;

			mpActiveFrame = NULL;
		}
	}
}

void VDVideoDisplayWindow::RequestNextFrame() {
	PostMessage(mhwnd, MYWM_PROCESSNEXTFRAME, 0, 0);
}

void VDVideoDisplayWindow::DispatchNextFrame() {
	vdsynchronized(mMutex) {
		VDASSERT(!mpActiveFrame);
		if (!mPendingFrames.empty()) {
			mpActiveFrame = mPendingFrames.front();
			mPendingFrames.pop_front();
		}
	}

	if (mpActiveFrame) {
		SetSource(false, mpActiveFrame->mPixmap, NULL, 0, mpActiveFrame->mbAllowConversion, mpActiveFrame->mbInterlaced);
		SyncUpdate(mpActiveFrame->mFlags);
	}
}

///////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDVideoDisplayWindow::StaticChildWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDVideoDisplayWindow *pThis = (VDVideoDisplayWindow *)GetWindowLongPtr(hwnd, 0);

	switch(msg) {
	case WM_NCCREATE:
		pThis = (VDVideoDisplayWindow *)(((LPCREATESTRUCT)lParam)->lpCreateParams);
		pThis->mhwndChild = hwnd;
		SetWindowLongPtr(hwnd, 0, (DWORD_PTR)pThis);
		break;
	case WM_NCDESTROY:
		SetWindowLongPtr(hwnd, 0, (DWORD_PTR)NULL);
		break;
	}

	return pThis ? pThis->ChildWndProc(msg, wParam, lParam) : DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT VDVideoDisplayWindow::ChildWndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_PAINT:
		OnChildPaint();
		return 0;
	case WM_NCHITTEST:
		if (mbIgnoreMouse)
			return HTTRANSPARENT;
		break;
	case WM_ERASEBKGND:
		return FALSE;
	}

	return DefWindowProc(mhwndChild, msg, wParam, lParam);
}

void VDVideoDisplayWindow::OnChildPaint() {
	++mInhibitRefresh;

	bool bDisplayOK = false;

	if (mpMiniDriver) {
		if (mpMiniDriver->IsValid())
			bDisplayOK = true;
		else if (mSource.pixmap.data && mSource.bPersistent && !mpMiniDriver->Update(IVDVideoDisplayMinidriver::kModeAllFields))
			bDisplayOK = true;
	}

	if (!bDisplayOK) {
		PAINTSTRUCT ps;
		BeginPaint(mhwndChild, &ps);
		EndPaint(mhwndChild, &ps);

		--mInhibitRefresh;
		RequestUpdate();
		return;
	}

	VDASSERT(IsWindow(mhwndChild));

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwndChild, &ps);

	if (hdc) {
		RECT r;

		GetClientRect(mhwndChild, &r);

		if (mpMiniDriver && mpMiniDriver->IsValid())
			VerifyDriverResult(mpMiniDriver->Paint(hdc, r, IVDVideoDisplayMinidriver::kModeAllFields));

		EndPaint(mhwndChild, &ps);
	}


	--mInhibitRefresh;
}

///////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDVideoDisplayWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDVideoDisplayWindow *pThis = (VDVideoDisplayWindow *)GetWindowLongPtr(hwnd, 0);

	switch(msg) {
	case WM_NCCREATE:
		pThis = new VDVideoDisplayWindow(hwnd, *(const CREATESTRUCT *)lParam);
		SetWindowLongPtr(hwnd, 0, (DWORD_PTR)pThis);
		break;
	case WM_NCDESTROY:
		if (pThis)
			pThis->SyncReset();
		delete pThis;
		pThis = NULL;
		SetWindowLongPtr(hwnd, 0, 0);
		break;
	}

	return pThis ? pThis->WndProc(msg, wParam, lParam) : DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT VDVideoDisplayWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_DESTROY:
		SyncReset();

		if (mReinitDisplayTimer) {
			KillTimer(mhwnd, mReinitDisplayTimer);
			mReinitDisplayTimer = 0;
		}

		if (mhOldPalette) {
			DeleteObject(mhOldPalette);
			mhOldPalette = 0;
		}

		break;
	case WM_PAINT:
		OnPaint();
		return 0;
	case MYWM_SETSOURCE:
		return SyncSetSource(wParam != 0, *(const VDVideoDisplaySourceInfo *)lParam);
	case MYWM_UPDATE:
		SyncUpdate((FieldMode)wParam);
		return 0;
	case MYWM_DESTROY:
		SyncReset();
		DestroyWindow(mhwnd);
		return 0;
	case MYWM_RESET:
		mMessage.clear();
		InvalidateRect(mhwnd, NULL, TRUE);
		SyncReset();
		mSource.pixmap.data = NULL;
		return 0;
	case MYWM_SETSOURCEMSG:
		SyncSetSourceMessage((const wchar_t *)lParam);
		return 0;
	case MYWM_PROCESSNEXTFRAME:
		if (!mpMiniDriver || !mpMiniDriver->IsFramePending()) {
			bool newframe;
			vdsynchronized(mMutex) {
				newframe = !mpActiveFrame;
			}

			if (newframe)
				DispatchNextFrame();
		}
		return 0;
	case MYWM_SETFILTERMODE:
		SyncSetFilterMode((FilterMode)lParam);
		return 0;
	case MYWM_SETSOLIDCOLOR:
		SyncSetSolidColor((uint32)lParam);
		return 0;
	case WM_SIZE:
		if (mhwndChild)
			SetWindowPos(mhwndChild, NULL, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOMOVE|SWP_NOCOPYBITS|SWP_NOZORDER|SWP_NOACTIVATE);
		if (mpMiniDriver)
			VerifyDriverResult(mpMiniDriver->Resize());
		break;
	case WM_TIMER:
		if (wParam == mReinitDisplayTimer) {
			SyncInit(true, false);
			return 0;
		} else {
			if (mpMiniDriver)
				VerifyDriverResult(mpMiniDriver->Tick((int)wParam));
		}
		break;
	case WM_NCHITTEST:
		if (mbIgnoreMouse) {
			LRESULT lr = DefWindowProc(mhwnd, msg, wParam, lParam);

			if (lr != HTCLIENT)
				return lr;
			return HTTRANSPARENT;
		}
		break;
	}

	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

void VDVideoDisplayWindow::OnPaint() {

	++mInhibitRefresh;
	bool bDisplayOK = false;

	if (mpMiniDriver) {
		if (mpMiniDriver->IsValid())
			bDisplayOK = true;
		else if (mSource.pixmap.data && mSource.bPersistent && !mpMiniDriver->Update(IVDVideoDisplayMinidriver::kModeAllFields))
			bDisplayOK = true;
	}

	if (!bDisplayOK) {
		--mInhibitRefresh;
		RequestUpdate();
		++mInhibitRefresh;
	}

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);

	if (hdc) {
		RECT r;

		GetClientRect(mhwnd, &r);

		FillRect(hdc, &r, (HBRUSH)(COLOR_3DFACE + 1));
		if (!mMessage.empty()) {
			HGDIOBJ hgo = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(hdc, TRANSPARENT);
			VDDrawTextW32(hdc, mMessage.data(), mMessage.size(), &r, DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_WORDBREAK);
			SelectObject(hdc, hgo);
		}

		EndPaint(mhwnd, &ps);
	}

	--mInhibitRefresh;
}

bool VDVideoDisplayWindow::SyncSetSource(bool bAutoUpdate, const VDVideoDisplaySourceInfo& params) {
	mCachedImage.clear();

	mSource = params;
	mMessage.clear();

	if (mpMiniDriver && mpMiniDriver->ModifySource(mSource)) {
		mpMiniDriver->SetColorOverride(0);

		mSource.bAllowConversion = true;

		if (bAutoUpdate)
			SyncUpdate(kAllFields);
		return true;
	}

	SyncReset();
	if (!SyncInit(bAutoUpdate, true))
		return false;

	mSource.bAllowConversion = true;
	return true;
}

void VDVideoDisplayWindow::SyncReset() {
	if (mpMiniDriver) {
		ShutdownMiniDriver();
		VDASSERT(!mpMiniDriver);

		SetPreciseMode(false);
		SetTicksEnabled(false);
	}
}

void VDVideoDisplayWindow::SyncSetSourceMessage(const wchar_t *msg) {
	if (!mpMiniDriver && mMessage == msg)
		return;

	SyncReset();
	mSource.pixmap.format = 0;
	mMessage = msg;
	InvalidateRect(mhwnd, NULL, TRUE);
}

bool VDVideoDisplayWindow::SyncInit(bool bAutoRefresh, bool bAllowNonpersistentSource) {
	if (!mSource.pixmap.data || !mSource.pixmap.format)
		return true;

	VDASSERT(!mpMiniDriver);

	bool bIsForeground = VDIsForegroundTaskW32();

	do {
		if (sbEnableTS || !VDIsTerminalServicesClient()) {
			if (mAccelMode != kAccelOnlyInForeground || !mSource.bAllowConversion || bIsForeground) {
				// The 3D drivers don't currently support subrects.
				if (sbEnableDX) {
					if (!mbUseSubrect) {
						if (sbEnableOGL) {
							mpMiniDriver = VDCreateVideoDisplayMinidriverOpenGL();
							if (InitMiniDriver())
								break;
							SyncReset();
						}

						if (sbEnableD3D) {
							if (sbEnableD3DFX)
								mpMiniDriver = VDCreateVideoDisplayMinidriverD3DFX();
							else
								mpMiniDriver = VDCreateVideoDisplayMinidriverDX9();
							if (InitMiniDriver())
								break;
							SyncReset();
						}
					}

					mpMiniDriver = VDCreateVideoDisplayMinidriverDirectDraw(sbEnableDXOverlay);
					if (InitMiniDriver())
						break;
					SyncReset();
				}

			} else {
				VDDEBUG_DISP("VideoDisplay: Application in background -- disabling accelerated preview.\n");
			}
		}

		mpMiniDriver = VDCreateVideoDisplayMinidriverGDI();
		if (InitMiniDriver())
			break;

		VDDEBUG_DISP("VideoDisplay: No driver was able to handle the requested format! (%d)\n", mSource.pixmap.format);
		SyncReset();
	} while(false);

	if (mpMiniDriver) {
		mpMiniDriver->SetLogicalPalette(GetLogicalPalette());
		mpMiniDriver->SetFilterMode((IVDVideoDisplayMinidriver::FilterMode)mFilterMode);
		mpMiniDriver->SetSubrect(mbUseSubrect ? &mSourceSubrect : NULL);

		if (mReinitDisplayTimer)
			KillTimer(mhwnd, mReinitDisplayTimer);

		if (bAutoRefresh) {
			if (bAllowNonpersistentSource)
				SyncUpdate(kAllFields);
			else
				RequestUpdate();
		}
	}

	return mpMiniDriver != 0;
}

void VDVideoDisplayWindow::SyncUpdate(int mode) {
	if (mSource.pixmap.data && !mpMiniDriver) {
		mSyncDelta = 0.0f;
		SyncInit(true, true);
		return;
	}

	if (mpMiniDriver) {
		bool vsync = 0 != (mode & kVSync);
		SetPreciseMode(vsync);
		SetTicksEnabled(vsync);

		mpMiniDriver->SetColorOverride(0);

		if (mode & kVisibleOnly) {
			bool bVisible = true;

			if (HDC hdc = GetDCEx(mhwnd, NULL, 0)) {
				RECT r;
				GetClientRect(mhwnd, &r);
				bVisible = 0 != RectVisible(hdc, &r);
				ReleaseDC(mhwnd, hdc);
			}

			mode = (FieldMode)(mode & ~kVisibleOnly);

			if (!bVisible)
				return;
		}

		mSyncDelta = 0.0f;

		bool success = mpMiniDriver->Update((IVDVideoDisplayMinidriver::UpdateMode)mode);
		ReleaseActiveFrame();
		if (success) {
			if (!mInhibitRefresh) {
				mpMiniDriver->Refresh((IVDVideoDisplayMinidriver::UpdateMode)mode);
				mSyncDelta = mpMiniDriver->GetSyncDelta();

				if (!mpMiniDriver->IsFramePending())
					RequestNextFrame();
			}
		}
	}
}

void VDVideoDisplayWindow::SyncCache() {
	if (mSource.pixmap.data && mSource.pixmap.data != mCachedImage.data) {
		mCachedImage.assign(mSource.pixmap);

		mSource.pixmap		= mCachedImage;
		mSource.bPersistent	= true;
	}

	if (mSource.pixmap.data && !mpMiniDriver)
		SyncInit(true, true);
}

void VDVideoDisplayWindow::SyncSetFilterMode(FilterMode mode) {
	if (mFilterMode != mode) {
		mFilterMode = mode;

		if (mpMiniDriver) {
			mpMiniDriver->SetFilterMode((IVDVideoDisplayMinidriver::FilterMode)mode);
			InvalidateRect(mhwnd, NULL, FALSE);
			InvalidateRect(mhwndChild, NULL, FALSE);
		}
	}
}

void VDVideoDisplayWindow::SyncSetSolidColor(uint32 color) {
	mSolidColorBuffer = color;

	VDVideoDisplaySourceInfo info;

	info.bAllowConversion	= true;
	info.bInterlaced		= false;
	info.bPersistent		= true;
	info.bpp				= 4;
	info.bpr				= 4;
	info.mpCB				= this;
	info.pixmap.data		= &mSolidColorBuffer;
	info.pixmap.format		= nsVDPixmap::kPixFormat_XRGB8888;
	info.pixmap.w			= 1;
	info.pixmap.h			= 1;
	info.pixmap.pitch		= 0;
	info.pSharedObject		= NULL;
	info.sharedOffset		= 0;

	SyncSetSource(false, info);

	if (mpMiniDriver) {
		mpMiniDriver->SetColorOverride(color);
		InvalidateRect(mhwnd, NULL, FALSE);
		InvalidateRect(mhwndChild, NULL, FALSE);
	}
}

void VDVideoDisplayWindow::OnDisplayChange() {
	HPALETTE hPal = GetPalette();
	if (mhOldPalette && !hPal) {
		if (HDC hdc = GetDC(mhwnd)) {
			SelectPalette(hdc, mhOldPalette, FALSE);
			mhOldPalette = 0;
			ReleaseDC(mhwnd, hdc);
		}
	}
	if (!mhOldPalette && hPal) {
		if (HDC hdc = GetDC(mhwnd)) {
			mhOldPalette = SelectPalette(hdc, hPal, FALSE);
			ReleaseDC(mhwnd, hdc);
		}
	}
	if (!mReinitDisplayTimer && !mbFullScreen) {
		SyncReset();
		if (!SyncInit(true, false))
			mReinitDisplayTimer = SetTimer(mhwnd, kReinitDisplayTimerId, 500, NULL);
	}
}

void VDVideoDisplayWindow::OnForegroundChange(bool bForeground) {
	if (mAccelMode != kAccelAlways)
		SyncReset();

	OnRealizePalette();
}

void VDVideoDisplayWindow::OnRealizePalette() {
	if (HDC hdc = GetDC(mhwnd)) {
		HPALETTE newPal = GetPalette();
		HPALETTE pal = SelectPalette(hdc, newPal, FALSE);
		if (!mhOldPalette)
			mhOldPalette = pal;
		RealizePalette(hdc);
		RemapPalette();

		if (mpMiniDriver) {
			mpMiniDriver->SetLogicalPalette(GetLogicalPalette());
			RequestUpdate();
		}

		ReleaseDC(mhwnd, hdc);
	}
}

bool VDVideoDisplayWindow::InitMiniDriver() {
	if (mhwndChild) {
		DestroyWindow(mhwndChild);
		mhwndChild = NULL;
	}

	RECT r;
	GetClientRect(mhwnd, &r);
	mhwndChild = CreateWindowEx(WS_EX_NOPARENTNOTIFY, (LPCTSTR)sChildWindowClass, "", WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS, 0, 0, r.right, r.bottom, mhwnd, NULL, VDGetLocalModuleHandleW32(), this);
	if (!mhwndChild)
		return false;

	mpMiniDriver->SetDisplayDebugInfo(sbEnableDebugInfo);
	mpMiniDriver->SetFullScreen(mbFullScreen);

	if (!mpMiniDriver->Init(mhwndChild, mSource)) {
		DestroyWindow(mhwndChild);
		mhwndChild = NULL;
		return false;
	}

	return true;
}

void VDVideoDisplayWindow::ShutdownMiniDriver() {
	if (mpMiniDriver) {
		// prevent recursion due to messages being triggered by Direct3D
		IVDVideoDisplayMinidriver *pMiniDriver = mpMiniDriver;
		mpMiniDriver = NULL;
		pMiniDriver->Shutdown();
		delete pMiniDriver;
	}

	if (mhwndChild) {
		DestroyWindow(mhwndChild);
		mhwndChild = NULL;
	}
}

void VDVideoDisplayWindow::RequestUpdate() {
	if (mpLastFrame) {
		if (!mpActiveFrame) {
			VDASSERT(!mpMiniDriver || !mpMiniDriver->IsFramePending());
			mpActiveFrame = mpLastFrame;
			mpLastFrame = NULL;
			SetSource(false, mpActiveFrame->mPixmap, NULL, 0, mpActiveFrame->mbAllowConversion, mpActiveFrame->mbInterlaced);
			SyncUpdate(mpActiveFrame->mFlags);
		}
	} else if (mpCB)
		mpCB->DisplayRequestUpdate(this);
	else if (mSource.pixmap.data && mSource.bPersistent) {
		SyncUpdate(kAllFields);
	}
}

void VDVideoDisplayWindow::VerifyDriverResult(bool result) {
	if (!result) {
		if (mpMiniDriver) {
			ShutdownMiniDriver();
		}

		if (!mReinitDisplayTimer)
			mReinitDisplayTimer = SetTimer(mhwnd, kReinitDisplayTimerId, 500, NULL);
	}
}

///////////////////////////////////////////////////////////////////////////////

vdautoptr<VDVideoDisplayManager> g_pVDVideoDisplayManager;

VDVideoDisplayManager *VDGetVideoDisplayManager() {
	if (!g_pVDVideoDisplayManager) {
		g_pVDVideoDisplayManager = new VDVideoDisplayManager;
		g_pVDVideoDisplayManager->Init();
	}

	return g_pVDVideoDisplayManager;
}

VDGUIHandle VDCreateDisplayWindowW32(uint32 dwExFlags, uint32 dwFlags, int x, int y, int width, int height, VDGUIHandle hwndParent) {
	VDVideoDisplayManager *vdm = VDGetVideoDisplayManager();

	if (!vdm)
		return NULL;

	struct RemoteCreateCall {
		DWORD dwExFlags;
		DWORD dwFlags;
		int x;
		int y;
		int width;
		int height;
		HWND hwndParent;
		VDVideoDisplayManager *vdm;
		HWND hwndResult;

		static void Dispatch(void *p0) {
			RemoteCreateCall *p = (RemoteCreateCall *)p0;
			p->hwndResult = CreateWindowEx(p->dwExFlags, g_szVideoDisplayControlName, "", p->dwFlags, p->x, p->y, p->width, p->height, p->hwndParent, NULL, VDGetLocalModuleHandleW32(), p->vdm);
		}
	} rmc = {dwExFlags, dwFlags | WS_CLIPCHILDREN, x, y, width, height, (HWND)hwndParent, vdm};

	vdm->RemoteCall(RemoteCreateCall::Dispatch, &rmc);
	return (VDGUIHandle)rmc.hwndResult;
}
