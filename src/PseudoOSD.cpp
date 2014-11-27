﻿#include <Windows.h>
#include <WindowsX.h>
#include <vector>
#include <string>
#include "Util.h"
#include "PseudoOSD.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

#if 0 // アセンブリ検索用
#define MAGIC_NUMBER(x) { g_dwMagic=(x); }
static DWORD g_dwMagic;
#else
#define MAGIC_NUMBER
#endif

#ifndef lengthof
#define lengthof _countof
#endif

// タイマーの識別子
#define TIMER_ID_HIDE		1
#define TIMER_ID_ANIMATION	2

#define ANIMATION_FRAMES	4	// アニメーションの段階数
#define ANIMATION_INTERVAL	50	// アニメーションの間隔




const LPCTSTR CPseudoOSD::m_pszWindowClass=APP_NAME TEXT(" Pseudo OSD");
HINSTANCE CPseudoOSD::m_hinst=NULL;
int CPseudoOSD::m_RefCount=0;

HBITMAP CPseudoOSD::m_hbmWork=NULL;
HBITMAP CPseudoOSD::m_hbmWorkMono=NULL;
void *CPseudoOSD::m_pBits=NULL;
void *CPseudoOSD::m_pBitsMono=NULL;


bool CPseudoOSD::Initialize(HINSTANCE hinst)
{
	if (m_hinst==NULL) {
		WNDCLASS wc;

		wc.style=CS_HREDRAW;
		wc.lpfnWndProc=WndProc;
		wc.cbClsExtra=0;
		wc.cbWndExtra=0;
		wc.hInstance=hinst;
		wc.hIcon=NULL;
		wc.hCursor=NULL;
		wc.hbrBackground=NULL;
		wc.lpszMenuName=NULL;
		wc.lpszClassName=m_pszWindowClass;
		if (::RegisterClass(&wc)==0)
			return false;
		m_hinst=hinst;
	}
	return true;
}


bool CPseudoOSD::IsPseudoOSD(HWND hwnd)
{
	TCHAR szClass[64];

	return ::GetClassName(hwnd,szClass,lengthof(szClass))>0
		&& ::lstrcmpi(szClass,m_pszWindowClass)==0;
}


CPseudoOSD::CPseudoOSD()
	: m_hwnd(NULL)
	, m_crBackColor(RGB(16,0,16))
	, m_crTextColor(RGB(0,255,128))
	, m_hbm(NULL)
	, m_TimerID(0)
	, m_AnimationCount(0)
	, m_Opacity(80)
	, m_BackOpacity(50)
	, m_StrokeWidth(0)
	, m_StrokeSmoothLevel(0)
	, m_fStrokeByDilate(false)
	, m_fHLLeft(false)
	, m_fHLTop(false)
	, m_fHLRight(false)
	, m_fHLBottom(false)
	, m_fLayeredWindow(false)
	, m_hwndParent(NULL)
	, m_hwndOwner(NULL)
	, m_fWindowPrepared(false)
{
	::SetRect(&m_ImagePaintRect,0,0,0,0);
	m_ParentPosition.x=0;
	m_ParentPosition.y=0;
	m_RefCount++;
}


CPseudoOSD::~CPseudoOSD()
{
	Destroy();
	if (--m_RefCount==0) {
		FreeWorkBitmap();
	}
}


bool CPseudoOSD::Create(HWND hwndParent,bool fLayeredWindow)
{
	if (m_hwnd!=NULL) {
		if (((fLayeredWindow && m_hwndParent==hwndParent && ::GetWindow(m_hwnd,GW_OWNER)==m_hwndOwner) ||
		     (!fLayeredWindow && ::GetParent(m_hwnd)==hwndParent))
				&& m_fLayeredWindow==fLayeredWindow)
			return true;
		Destroy();
	}
	m_fLayeredWindow=fLayeredWindow;
	m_hwndParent=hwndParent;
	if (fLayeredWindow) {
		POINT pt;
		RECT rc;

		pt.x=m_Position.Left;
		pt.y=m_Position.Top;
		::ClientToScreen(hwndParent,&pt);
		if (::CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
							 m_pszWindowClass,NULL,WS_POPUP,
							 pt.x,pt.y,m_Position.GetEntireWidth(),m_Position.Height,
							 hwndParent,NULL,m_hinst,this)==NULL)
			return false;
		/*
		::SetLayeredWindowAttributes(m_hwnd,m_crBackColor,m_Opacity*255/100,
									 LWA_COLORKEY | LWA_ALPHA);
		*/
		::GetWindowRect(hwndParent,&rc);
		m_ParentPosition.x=rc.left;
		m_ParentPosition.y=rc.top;
		// WS_POPUPに親はいない。hwndParentからトップレベルまで遡ったウィンドウがオーナーになる
		m_hwndOwner = ::GetWindow(m_hwnd,GW_OWNER);
		return true;
	}
	return ::CreateWindowEx(0,m_pszWindowClass,NULL,WS_CHILD,
							m_Position.Left,m_Position.Top,
							m_Position.GetEntireWidth(),m_Position.Height,
							hwndParent,NULL,m_hinst,this)!=NULL;
}


bool CPseudoOSD::Destroy()
{
	if (m_hwnd!=NULL)
		::DestroyWindow(m_hwnd);
	ClearText();
	SetImage(NULL,0);
	return true;
}


// ウィンドウの描画作業を完了する
bool CPseudoOSD::PrepareWindow(DWORD Time,bool fAnimation)
{
	if (m_hwnd==NULL)
		return false;

	m_fWindowPrepared=true;
	if (m_fLayeredWindow) {
		if (Time>0) {
			POINT pt;

			pt.x=m_Position.Left;
			pt.y=m_Position.Top;
			::ClientToScreen(m_hwndParent,&pt);
			m_TimerID|=::SetTimer(m_hwnd,TIMER_ID_HIDE,Time,NULL);
			if (fAnimation) {
				m_AnimationCount=0;
				::SetWindowPos(m_hwnd,NULL,pt.x,pt.y,
							   m_Position.GetEntireWidth()/ANIMATION_FRAMES,m_Position.Height,
							   SWP_NOZORDER | SWP_NOACTIVATE);
				m_TimerID|=::SetTimer(m_hwnd,TIMER_ID_ANIMATION,ANIMATION_INTERVAL,NULL);
			} else {
				::SetWindowPos(m_hwnd,NULL,pt.x,pt.y,
							   m_Position.GetEntireWidth(),m_Position.Height,
							   SWP_NOZORDER | SWP_NOACTIVATE);
			}
		} else if ((m_TimerID&TIMER_ID_HIDE)!=0) {
			::KillTimer(m_hwnd,TIMER_ID_HIDE);
			m_TimerID&=~TIMER_ID_HIDE;
		}
		UpdateLayeredWindow();
		return true;
	}

	if (Time>0) {
		m_TimerID|=::SetTimer(m_hwnd,TIMER_ID_HIDE,Time,NULL);
		if (fAnimation) {
			m_AnimationCount=0;
			::MoveWindow(m_hwnd,m_Position.Left,m_Position.Top,
						 m_Position.GetEntireWidth()/ANIMATION_FRAMES,m_Position.Height,
						 TRUE);
			m_TimerID|=::SetTimer(m_hwnd,TIMER_ID_ANIMATION,ANIMATION_INTERVAL,NULL);
		} else {
			::MoveWindow(m_hwnd,m_Position.Left,m_Position.Top,
						 m_Position.GetEntireWidth(),m_Position.Height,TRUE);
		}
	} else if ((m_TimerID&TIMER_ID_HIDE)!=0) {
		::KillTimer(m_hwnd,TIMER_ID_HIDE);
		m_TimerID&=~TIMER_ID_HIDE;
	}
	return true;
}


bool CPseudoOSD::Show(DWORD Time,bool fAnimation)
{
	if (!m_fWindowPrepared)
		PrepareWindow(Time,fAnimation);
	m_fWindowPrepared=false;

	if (m_hwnd==NULL)
		return false;

	if (m_fLayeredWindow) {
		// 実際には親子関係じゃないので自力で可視になるか判断する必要がある
		if (::IsWindowVisible(m_hwndParent)) {
			::ShowWindow(m_hwnd,SW_SHOWNOACTIVATE);
		}
		::UpdateWindow(m_hwnd);

		RECT rc;
		::GetWindowRect(m_hwndParent,&rc);
		m_ParentPosition.x=rc.left;
		m_ParentPosition.y=rc.top;
		return true;
	}
	if (::IsWindowVisible(m_hwnd)) {
		::RedrawWindow(m_hwnd,NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW);
	} else {
		::ShowWindow(m_hwnd,SW_SHOW);
		::BringWindowToTop(m_hwnd);
		::UpdateWindow(m_hwnd);
	}
	return true;
}


bool CPseudoOSD::Hide()
{
	if (m_hwnd==NULL)
		return false;
	::ShowWindow(m_hwnd,SW_HIDE);
	ClearText();
	SetImage(NULL,0);
	return true;
}


bool CPseudoOSD::IsVisible() const
{
	if (m_hwnd==NULL)
		return false;
	return ::IsWindowVisible(m_hwnd)!=FALSE;
}


void CPseudoOSD::ClearText()
{
	if (!m_Position.StyleList.empty()) {
		m_Position.StyleList.clear();
		SetPosition(m_Position.Left,m_Position.Top,m_Position.Height);
	}
}


bool CPseudoOSD::AddText(LPCTSTR pszText,int Width,const LOGFONT &lf)
{
	SetImage(NULL,0);
	CWindowStyle st(pszText,Width,lf);
	m_Position.StyleList.push_back(st);
	SetPosition(m_Position.Left,m_Position.Top,m_Position.Height);
	/*
	if (IsVisible()) {
		if (m_fLayeredWindow)
			UpdateLayeredWindow();
		else
			::RedrawWindow(m_hwnd,NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW);
	}
	*/
	return true;
}


bool CPseudoOSD::SetPosition(int Left,int Top,int Height)
{
	if (Height<=0)
		return false;
	m_Position.Left=Left;
	m_Position.Top=Top;
	m_Position.Height=Height;
	if (m_hwnd!=NULL) {
		if (m_fLayeredWindow) {
			POINT pt;

			pt.x=Left;
			pt.y=Top;
			::ClientToScreen(m_hwndParent,&pt);
			::SetWindowPos(m_hwnd,NULL,pt.x,pt.y,m_Position.GetEntireWidth(),Height,
						   SWP_NOZORDER | SWP_NOACTIVATE);
		} else {
			::SetWindowPos(m_hwnd,HWND_TOP,Left,Top,m_Position.GetEntireWidth(),Height,0);
		}
	}
	return true;
}


void CPseudoOSD::GetPosition(int *pLeft,int *pTop,int *pWidth,int *pHeight) const
{
	if (pLeft)
		*pLeft=m_Position.Left;
	if (pTop)
		*pTop=m_Position.Top;
	if (pWidth)
		*pWidth=m_Position.GetEntireWidth();
	if (pHeight)
		*pHeight=m_Position.Height;
}


void CPseudoOSD::SetTextColor(COLORREF crText,COLORREF crBack)
{
	m_crTextColor=crText;
	m_crBackColor=crBack;
	/*
	if (m_hwnd!=NULL)
		::InvalidateRect(m_hwnd,NULL,TRUE);
	*/
}


// 受けとったビットマップはクラス側で破棄する
bool CPseudoOSD::SetImage(HBITMAP hbm,int Width,const RECT *pPaintRect)
{
	if (hbm==NULL) {
		// ClearText()と対称
		if (m_hbm!=NULL) {
			::DeleteObject(m_hbm);
			m_hbm=NULL;
			m_Position.ImageWidth=0;
			SetPosition(m_Position.Left,m_Position.Top,m_Position.Height);
		}
		return true;
	}
	// AddText()と対称
	ClearText();
	if (m_hbm!=NULL)
		::DeleteObject(m_hbm);
	m_hbm=hbm;
	m_Position.ImageWidth=Width;
	if (pPaintRect) {
		m_ImagePaintRect=*pPaintRect;
	} else {
		::SetRect(&m_ImagePaintRect,0,0,0,0);
	}
	SetPosition(m_Position.Left,m_Position.Top,m_Position.Height);
#if 0
	if (m_hwnd!=NULL) {
		/*
		BITMAP bm;

		::GetObject(m_hbm,sizeof(BITMAP),&bm);
		m_Position.Width=bm.bmWidth;
		m_Position.Height=bm.bmHeight;
		::MoveWindow(m_hwnd,Left,Top,bm.bmWidth,bm.bmHeight,TRUE);
		*/
		if (m_fLayeredWindow)
			UpdateLayeredWindow();
		else
			::RedrawWindow(m_hwnd,NULL,NULL,RDW_INVALIDATE | RDW_UPDATENOW);
	}
#endif
	return true;
}


bool CPseudoOSD::SetOpacity(int Opacity,int BackOpacity)
{
	m_Opacity=Opacity;
	m_BackOpacity=BackOpacity;
	return true;
}


void CPseudoOSD::SetStroke(int Width,int SmoothLevel,bool fStrokeByDilate)
{
	m_StrokeWidth=Width;
	m_StrokeSmoothLevel=SmoothLevel;
	m_fStrokeByDilate=fStrokeByDilate;
}


void CPseudoOSD::SetHighlightingBlock(bool fLeft,bool fTop,bool fRight,bool fBottom)
{
	m_fHLLeft=fLeft;
	m_fHLTop=fTop;
	m_fHLRight=fRight;
	m_fHLBottom=fBottom;
}


void CPseudoOSD::OnParentMove()
{
	if (m_hwnd!=NULL && m_fLayeredWindow) {
		RECT rcParent,rc;

		::GetWindowRect(m_hwndParent,&rcParent);
		::GetWindowRect(m_hwnd,&rc);
		::OffsetRect(&rc,
					 rcParent.left-m_ParentPosition.x,
					 rcParent.top-m_ParentPosition.y);
		::SetWindowPos(m_hwnd,NULL,rc.left,rc.top,
					   rc.right-rc.left,rc.bottom-rc.top,
					   SWP_NOZORDER | SWP_NOACTIVATE);
		m_ParentPosition.x=rcParent.left;
		m_ParentPosition.y=rcParent.top;
	}
}


static BOOL TextOutMonospace(HDC hdc,int x,int y,LPCTSTR lpString,UINT cbCount,int Width,int Mult)
{
	INT dx[1024];
	cbCount=min(cbCount,lengthof(dx));
	for (UINT i=0; i<cbCount; i++) {
		dx[i]=Width/(cbCount-i)*Mult;
		Width-=Width/(cbCount-i);
	}
	return ::ExtTextOut(hdc,x*Mult,y*Mult,0,NULL,lpString,cbCount,dx);
}

static void DrawLine(HDC hdc,int bx,int by,int ex,int ey,COLORREF cr)
{
	LOGBRUSH lb;
	lb.lbStyle=BS_SOLID;
	lb.lbColor=cr;
	lb.lbHatch=0;

	HPEN hPen=::ExtCreatePen(PS_SOLID|PS_GEOMETRIC|PS_ENDCAP_SQUARE,2,&lb,0,NULL);
	if (hPen) {
		HGDIOBJ hPenOld=::SelectObject(hdc,hPen);
		POINT lastPos;
		::MoveToEx(hdc,bx,by,&lastPos);
		::LineTo(hdc,ex,ey);
		::MoveToEx(hdc,lastPos.x,lastPos.y,NULL);
		::SelectObject(hdc,hPenOld);
		::DeleteObject(hPen);
	}
}

void CPseudoOSD::DrawTextList(HDC hdc,int Mult) const
{
	int x=0;
	std::vector<CWindowStyle>::const_iterator it = m_Position.StyleList.begin();
	for (; it!=m_Position.StyleList.end(); ++it) {
		DrawUtil::CFont Font;
		LOGFONT lf=it->lf;
		lf.lfWidth*=Mult;
		lf.lfHeight*=Mult;
		if (!it->Text.empty() && Font.Create(&lf)) {
			HFONT hfontOld=DrawUtil::SelectObject(hdc,Font);
			int intvX=it->Width/(int)it->Text.length() - it->lf.lfWidth*2;
			int intvY=m_Position.Height - (it->lf.lfHeight<0?-it->lf.lfHeight:it->lf.lfHeight);
			TextOutMonospace(hdc,x+intvX/2,intvY/2,it->Text.c_str(),(int)it->Text.length(),it->Width-intvX,Mult);
			::SelectObject(hdc,hfontOld);
		}
		x+=it->Width;
	}
}

void CPseudoOSD::Draw(HDC hdc,const RECT &PaintRect) const
{
	RECT rc;

	::GetClientRect(m_hwnd,&rc);
	DrawUtil::Fill(hdc,&rc,m_crBackColor);

	if (m_fHLLeft) DrawLine(hdc,1,rc.bottom-1,1,1,m_crTextColor);
	if (m_fHLTop) DrawLine(hdc,1,1,rc.right-1,1,m_crTextColor);
	if (m_fHLRight) DrawLine(hdc,rc.right-1,1,rc.right-1,rc.bottom-1,m_crTextColor);
	if (m_fHLBottom) DrawLine(hdc,rc.right-1,rc.bottom-1,1,rc.bottom-1,m_crTextColor);

	if (!m_Position.StyleList.empty()) {
		COLORREF crOldTextColor;
		int OldBkMode;

		crOldTextColor=::SetTextColor(hdc,m_crTextColor);
		OldBkMode=::SetBkMode(hdc,TRANSPARENT);
		DrawTextList(hdc,1);
		::SetBkMode(hdc,OldBkMode);
		::SetTextColor(hdc,crOldTextColor);
	} else if (m_hbm!=NULL) {
		BITMAP bm;
		::GetObject(m_hbm,sizeof(BITMAP),&bm);
		RECT rcBitmap={0,0,bm.bmWidth,bm.bmHeight};
		if (m_ImagePaintRect.right!=0 && m_ImagePaintRect.bottom!=0) {
			DrawUtil::DrawBitmap(hdc,m_ImagePaintRect.left,m_ImagePaintRect.top,
			                     m_ImagePaintRect.right,m_ImagePaintRect.bottom,m_hbm,&rcBitmap);
		} else {
			DrawUtil::DrawBitmap(hdc,0,0,rc.right,rc.bottom,m_hbm,&rcBitmap);
		}
	}
}

// 表示中のOSDのイメージをhdcに合成する
void CPseudoOSD::Compose(HDC hdc,int Left,int Top)
{
	if (hdc==NULL || !IsVisible()) return;
	RECT rc;
	::GetClientRect(m_hwnd,&rc);
	if (rc.right<1 || rc.bottom<1) return;

	// OSDのイメージを描画する一時ビットマップ
	void *pBits;
	BITMAPINFO bmi={0};
	bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth=rc.right;
	bmi.bmiHeader.biHeight=-rc.bottom;
	bmi.bmiHeader.biPlanes=1;
	bmi.bmiHeader.biBitCount=32;
	bmi.bmiHeader.biCompression=BI_RGB;
	HBITMAP hbmTmp=::CreateDIBSection(NULL,&bmi,DIB_RGB_COLORS,&pBits,NULL,0);
	if (hbmTmp==NULL) return;

	HDC hdcTmp=::CreateCompatibleDC(hdc);
	HBITMAP hbmOld=static_cast<HBITMAP>(::SelectObject(hdcTmp,hbmTmp));
	if (m_fLayeredWindow) {
		// アルファ合成のために背景をコピーしておく
		::BitBlt(hdcTmp,0,0,rc.right,rc.bottom,hdc,Left,Top,SRCCOPY);
		UpdateLayeredWindow(hdcTmp,pBits,rc.right,rc.bottom);
	} else {
		Draw(hdcTmp,rc);
	}
	::BitBlt(hdc,Left,Top,rc.right,rc.bottom,hdcTmp,0,0,SRCCOPY);
	::SelectObject(hdcTmp,hbmOld);
	::DeleteDC(hdcTmp);
	::DeleteObject(hbmTmp);
}

// 縦横4倍の2値画像をもとにアルファチャネルを構成する
static void SetBitmapOpacity(void *pBits,int Width,const RECT &Rect,const void *pBitsMono,BYTE TextOpacity,BYTE BackOpacity)
{
	MAGIC_NUMBER(0x77867563);
	if (Rect.left%2!=0) return;
	static const int BitSum[16]={ 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 };

	int level=BackOpacity;
	int range=TextOpacity-BackOpacity;
	int stride=(Width*4+31)/32*4;
	for (int y=Rect.top;y<Rect.bottom;y++) {
		BYTE *p=static_cast<BYTE*>(pBits)+(y*Width+Rect.left)*4;
		LPCBYTE q0=static_cast<LPCBYTE>(pBitsMono)+(y*4+0)*stride+Rect.left/2;
		LPCBYTE q1=static_cast<LPCBYTE>(pBitsMono)+(y*4+1)*stride+Rect.left/2;
		LPCBYTE q2=static_cast<LPCBYTE>(pBitsMono)+(y*4+2)*stride+Rect.left/2;
		LPCBYTE q3=static_cast<LPCBYTE>(pBitsMono)+(y*4+3)*stride+Rect.left/2;
		int x=Rect.left/2;
		for (;x<Rect.right/2;x++) {
			p[3]=(BYTE)(level+(BitSum[*q0>>4]+BitSum[*q1>>4]+BitSum[*q2>>4]+BitSum[*q3>>4])*range/16);
			p[7]=(BYTE)(level+(BitSum[*q0&15]+BitSum[*q1&15]+BitSum[*q2&15]+BitSum[*q3&15])*range/16);
			p+=8;
			q0++;
			q1++;
			q2++;
			q3++;
		}
		if (x*2<Rect.right) {
			p[3]=(BYTE)(level+(BitSum[*q0>>4]+BitSum[*q1>>4]+BitSum[*q2>>4]+BitSum[*q3>>4])*range/16);
		}
	}
}

// crKeyな画素をアルファチャネルに写す
static void LayBitmapToAlpha(void *pBits,int Width,const RECT &Rect,BYTE TextOpacity,COLORREF crKey)
{
	MAGIC_NUMBER(0x66735267);
	DWORD dwKey=(GetRValue(crKey)<<16)|(GetGValue(crKey)<<8)|GetBValue(crKey);
	for (int y=Rect.top;y<Rect.bottom;y++) {
		DWORD *p=static_cast<DWORD*>(pBits)+y*Width+Rect.left;
		for (int x=Rect.left;x<Rect.right;x++) {
			if ((*p&0x00FFFFFF)!=dwKey)
				*p=(TextOpacity<<24)|(*p&0x00FFFFFF);
			p++;
		}
	}
}

static void PremultiplyBitmap(void *pBits,int Width,const RECT &Rect)
{
	MAGIC_NUMBER(0x67683625);
	for (int y=Rect.top;y<Rect.bottom;y++) {
		BYTE *p=static_cast<BYTE*>(pBits)+(y*Width+Rect.left)*4;
		for (int x=Rect.left;x<Rect.right;x++) {
			p[0]=(p[0]*p[3]+255)>>8;
			p[1]=(p[1]*p[3]+255)>>8;
			p[2]=(p[2]*p[3]+255)>>8;
			p+=4;
		}
	}
}

// Opacityなアルファチャネルを膨張させる
static void DilateAlpha(void *pBits,int Width,const RECT &Rect,BYTE Opacity)
{
	MAGIC_NUMBER(0x94275645);
	BYTE LastLine[1+8192];
	if (Rect.right-Rect.left<=1 || 1+(Rect.right-Rect.left)>lengthof(LastLine)) return;

	::ZeroMemory(LastLine,1+(Rect.right-Rect.left));
	for (int y=Rect.top;y<Rect.bottom-1;y++) {
		BYTE *pp=LastLine+1;
		BYTE *q=static_cast<BYTE*>(pBits)+(y*Width+Rect.left)*4;
		BYTE *r=static_cast<BYTE*>(pBits)+((y+1)*Width+Rect.left)*4;
		BYTE LastPixel[3]={0,0,0};
		for (int x=Rect.left;x<Rect.right-1;x++) {
			BYTE i[9]={LastPixel[0],pp[0],pp[1],
			           LastPixel[1],q[3],q[4+3],
			           LastPixel[2],r[3],r[4+3]};
			pp[-1]=LastPixel[1];
			LastPixel[0]=i[1];
			LastPixel[1]=i[4];
			LastPixel[2]=i[7];
			if (i[0]==Opacity||i[1]==Opacity||i[2]==Opacity||i[3]==Opacity||
			    i[5]==Opacity||i[6]==Opacity||i[7]==Opacity||i[8]==Opacity) q[3]=Opacity;
			pp++;
			q+=4;
			r+=4;
		}
		// 右端処理
		BYTE i[9]={LastPixel[0],pp[0],0,
		           LastPixel[1],q[3],0,
		           LastPixel[2],r[3],0};
		if (i[0]==Opacity||i[1]==Opacity||i[2]==Opacity||i[3]==Opacity||
		    i[5]==Opacity||i[6]==Opacity||i[7]==Opacity||i[8]==Opacity) q[3]=Opacity;
	}
}

// アルファチャネルを平滑化する
static void SmoothAlpha(void *pBits,int Width,const RECT &Rect,BYTE EdgeOpacity,int SmoothLevel)
{
	MAGIC_NUMBER(0x47673283);
	static const BYTE Coefs[5][10]={
		{1,5,1,5,25,5,1,5,1,49},
		{1,4,1,4,16,4,1,4,1,36},
		{1,3,1,3,9,3,1,3,1,25},
		{1,2,1,2,4,2,1,2,1,16},
		{1,1,1,1,1,1,1,1,1,9},
	};
	BYTE LastLine[1+8192];
	if (SmoothLevel<0 || lengthof(Coefs)<=SmoothLevel ||
	    Rect.right-Rect.left<=1 || 1+(Rect.right-Rect.left)>lengthof(LastLine)) return;

	const BYTE *c=Coefs[SmoothLevel];
	::FillMemory(LastLine,1+(Rect.right-Rect.left),EdgeOpacity);
	for (int y=Rect.top;y<Rect.bottom-1;y++) {
		BYTE *pp=LastLine+1;
		BYTE *q=static_cast<BYTE*>(pBits)+(y*Width+Rect.left)*4;
		BYTE *r=static_cast<BYTE*>(pBits)+((y+1)*Width+Rect.left)*4;
		BYTE LastPixel[3]={EdgeOpacity,EdgeOpacity,EdgeOpacity};
		for (int x=Rect.left;x<Rect.right-1;x++) {
			BYTE i[9]={LastPixel[0],pp[0],pp[1],
			           LastPixel[1],q[3],q[4+3],
			           LastPixel[2],r[3],r[4+3]};
			pp[-1]=LastPixel[1];
			LastPixel[0]=i[1];
			LastPixel[1]=i[4];
			LastPixel[2]=i[7];
			q[3]=(BYTE)((i[0]*c[0]+i[1]*c[1]+i[2]*c[2]+
			             i[3]*c[3]+i[4]*c[4]+i[5]*c[5]+
			             i[6]*c[6]+i[7]*c[7]+i[8]*c[8])/c[9]);
			pp++;
			q+=4;
			r+=4;
		}
		// 右端処理
		BYTE i[9]={LastPixel[0],pp[0],EdgeOpacity,
		           LastPixel[1],q[3],EdgeOpacity,
		           LastPixel[2],r[3],EdgeOpacity};
		q[3]=(BYTE)((i[0]*c[0]+i[1]*c[1]+i[2]*c[2]+
		             i[3]*c[3]+i[4]*c[4]+i[5]*c[5]+
		             i[6]*c[6]+i[7]*c[7]+i[8]*c[8])/c[9]);
	}
}

// アルファ合成する
static void ComposeAlpha(void *pBitsDest,int WidthDest,const void *pBits,int Width,const RECT &Rect)
{
	MAGIC_NUMBER(0x36481956);
	for (int y=Rect.top,yy=0;y<Rect.bottom;y++,yy++) {
		const BYTE *p=static_cast<const BYTE*>(pBits)+(y*Width+Rect.left)*4;
		BYTE *q=static_cast<BYTE*>(pBitsDest)+(yy*WidthDest)*4;
		for (int x=Rect.left;x<Rect.right;x++) {
			q[0]=((p[0]<<8) + (q[0]*(255-p[3])+255))>>8;
			q[1]=((p[1]<<8) + (q[1]*(255-p[3])+255))>>8;
			q[2]=((p[2]<<8) + (q[2]*(255-p[3])+255))>>8;
			q[3]=0;
			p+=4;
			q+=4;
		}
	}
}

void CPseudoOSD::FreeWorkBitmap()
{
	if (m_hbmWork!=NULL) {
		::DeleteObject(m_hbmWork);
		::DeleteObject(m_hbmWorkMono);
		m_hbmWork=NULL;
		m_hbmWorkMono=NULL;
	}
}

// 作業用ビットマップを確保
// 全オブジェクトで共用(状況によってはMB単位の確保と解放を秒単位でくり返すことになるため)
bool CPseudoOSD::AllocateWorkBitmap(int Width,int Height,int *pAllocWidth,int *pAllocHeight)
{
	if (m_hbmWork!=NULL) {
		BITMAP bm;
		if (::GetObject(m_hbmWork,sizeof(BITMAP),&bm) &&
			bm.bmWidth>=Width && bm.bmHeight>=Height) {
			*pAllocWidth=bm.bmWidth;
			*pAllocHeight=bm.bmHeight;
			return true;
		}
		FreeWorkBitmap();
	}
	BITMAPINFO bmi;
	::ZeroMemory(&bmi,sizeof(bmi));
	bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth=Width;
	bmi.bmiHeader.biHeight=-Height;
	bmi.bmiHeader.biPlanes=1;
	bmi.bmiHeader.biBitCount=32;
	bmi.bmiHeader.biCompression=BI_RGB;
	m_hbmWork=::CreateDIBSection(NULL,&bmi,DIB_RGB_COLORS,&m_pBits,NULL,0);
	if (m_hbmWork==NULL) {
		return false;
	}

	// アルファチャネル描画用の縦横4倍の2値画像
	struct {
		BITMAPINFOHEADER bmiHeader;
		RGBQUAD bmiColors[2];
	} bmiMono={0};
	bmiMono.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bmiMono.bmiHeader.biWidth=Width*4;
	bmiMono.bmiHeader.biHeight=-Height*4;
	bmiMono.bmiHeader.biPlanes=1;
	bmiMono.bmiHeader.biBitCount=1;
	bmiMono.bmiHeader.biCompression=BI_RGB;
	bmiMono.bmiColors[1].rgbBlue=255;
	bmiMono.bmiColors[1].rgbGreen=255;
	bmiMono.bmiColors[1].rgbRed=255;
	m_hbmWorkMono=::CreateDIBSection(NULL,(BITMAPINFO*)&bmiMono,DIB_RGB_COLORS,&m_pBitsMono,NULL,0);
	if (m_hbmWorkMono==NULL) {
		::DeleteObject(m_hbmWork);
		m_hbmWork=NULL;
		return false;
	}
	*pAllocWidth=Width;
	*pAllocHeight=Height;
	return true;
}

// hdcCompose!=NULLのとき、このデバイスコンテキストのpBitsCompose(32bitDIBビット値)に合成する
void CPseudoOSD::UpdateLayeredWindow(HDC hdcCompose,void *pBitsCompose,int WidthCompose,int HeightCompose)
{
	RECT rc;
	if (hdcCompose) {
		::SetRect(&rc,0,0,WidthCompose,HeightCompose);
	} else {
		::GetClientRect(m_hwnd,&rc);
	}
	int Width=rc.right;
	int Height=rc.bottom;
	if (Width<1 || Height<1)
		return;

	int AllocWidth,AllocHeight;
	if (!AllocateWorkBitmap(Width,Height,&AllocWidth,&AllocHeight))
		return;

	HDC hdc,hdcSrc,hdcMono;
	if (hdcCompose) {
		hdc=NULL;
		hdcSrc=::CreateCompatibleDC(hdcCompose);
		hdcMono=::CreateCompatibleDC(hdcCompose);
	} else {
		hdc=::GetDC(m_hwnd);
		hdcSrc=::CreateCompatibleDC(hdc);
		hdcMono=::CreateCompatibleDC(hdc);
	}
	HBITMAP hbmOld=static_cast<HBITMAP>(::SelectObject(hdcSrc,m_hbmWork));
	HBITMAP hbmMonoOld=static_cast<HBITMAP>(::SelectObject(hdcMono,m_hbmWorkMono));

	DrawUtil::Fill(hdcSrc,&rc,m_crBackColor);
	::ZeroMemory(m_pBitsMono,(AllocWidth*4+31)/32*4 * Height*4);

	bool fNeedToLay=false;
	bool fNeedToDilate=false;
	if (m_fHLLeft||m_fHLTop||m_fHLRight||m_fHLBottom) {
		if (m_fHLLeft) DrawLine(hdcSrc,1,rc.bottom-1,1,1,m_crTextColor);
		if (m_fHLTop) DrawLine(hdcSrc,1,1,rc.right-1,1,m_crTextColor);
		if (m_fHLRight) DrawLine(hdcSrc,rc.right-1,1,rc.right-1,rc.bottom-1,m_crTextColor);
		if (m_fHLBottom) DrawLine(hdcSrc,rc.right-1,rc.bottom-1,1,rc.bottom-1,m_crTextColor);
		fNeedToLay=true;
	}

	if (!m_Position.StyleList.empty()) {
		COLORREF crOldTextColor;
		int OldBkMode;

		crOldTextColor=::SetTextColor(hdcSrc,m_crTextColor);
		OldBkMode=::SetBkMode(hdcSrc,TRANSPARENT);
		DrawTextList(hdcSrc,1);
		::SetBkMode(hdcSrc,OldBkMode);
		::SetTextColor(hdcSrc,crOldTextColor);

		if (m_fStrokeByDilate) {
			fNeedToLay=true;
			fNeedToDilate=true;
		}
		else {
			// 縁取りを描画
			HPEN hpen=(HPEN)::CreatePen(PS_SOLID,m_StrokeWidth<=0?0:m_StrokeWidth*8/72+8,RGB(255,255,255));
			if (hpen!=NULL) {
				HPEN hpenOld=SelectPen(hdcMono,hpen);
				HBRUSH hbr=(HBRUSH)::GetStockObject(WHITE_BRUSH);
				if (hbr!=NULL) {
					HBRUSH hbrOld=SelectBrush(hdcMono,hbr);
					OldBkMode=::SetBkMode(hdcMono,TRANSPARENT);
					::BeginPath(hdcMono);
					DrawTextList(hdcMono,4);
					::EndPath(hdcMono);
					::StrokeAndFillPath(hdcMono);
					::SetBkMode(hdcMono,OldBkMode);
					SelectBrush(hdcMono,hbrOld);
				}
				SelectPen(hdcMono,hpenOld);
				::DeletePen(hpen);
			}
		}
	} else if (m_hbm!=NULL) {
		BITMAP bm;
		::GetObject(m_hbm,sizeof(BITMAP),&bm);
		RECT rcBitmap={0,0,bm.bmWidth,bm.bmHeight};
		if (m_ImagePaintRect.right!=0 && m_ImagePaintRect.bottom!=0) {
			DrawUtil::DrawBitmap(hdcSrc,m_ImagePaintRect.left,m_ImagePaintRect.top,
			                     m_ImagePaintRect.right,m_ImagePaintRect.bottom,m_hbm,&rcBitmap);
		} else {
			DrawUtil::DrawBitmap(hdcSrc,0,0,Width,Height,m_hbm,&rcBitmap);
		}
		fNeedToLay=true;
		fNeedToDilate=true;
	}
	::GdiFlush();

	// アルファチャネル操作
	SetBitmapOpacity(m_pBits,AllocWidth,rc,m_pBitsMono,(BYTE)(m_Opacity*255/100),(BYTE)(m_BackOpacity*255/100));
	if (fNeedToLay) {
		LayBitmapToAlpha(m_pBits,AllocWidth,rc,(BYTE)(m_Opacity*255/100),m_crBackColor);
	}
	if (fNeedToDilate) {
		for (int i=0; i<(m_StrokeWidth+36)/72; i++) {
			DilateAlpha(m_pBits,AllocWidth,rc,(BYTE)(m_Opacity*255/100));
		}
	}
	SmoothAlpha(m_pBits,AllocWidth,rc,(BYTE)(m_BackOpacity*255/100),m_StrokeSmoothLevel-(m_fStrokeByDilate?1:2));
	PremultiplyBitmap(m_pBits,AllocWidth,rc);

	if (hdcCompose) {
		ComposeAlpha(pBitsCompose,Width,m_pBits,AllocWidth,rc);
	} else {
		SIZE sz={Width,Height};
		POINT ptSrc={0,0};
		BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
		::UpdateLayeredWindow(m_hwnd,hdc,NULL,&sz,hdcSrc,&ptSrc,0,&blend,ULW_ALPHA);
	}
	::SelectObject(hdcMono,hbmMonoOld);
	::DeleteDC(hdcMono);

	::SelectObject(hdcSrc,hbmOld);
	::DeleteDC(hdcSrc);
	if (hdc) ::ReleaseDC(m_hwnd,hdc);
}


CPseudoOSD *CPseudoOSD::GetThis(HWND hwnd)
{
	return reinterpret_cast<CPseudoOSD*>(::GetWindowLongPtr(hwnd,GWLP_USERDATA));
}


LRESULT CALLBACK CPseudoOSD::WndProc(HWND hwnd,UINT uMsg,
												WPARAM wParam,LPARAM lParam)
{
	switch (uMsg) {
	case WM_CREATE:
		{
			LPCREATESTRUCT pcs=reinterpret_cast<LPCREATESTRUCT>(lParam);
			CPseudoOSD *pThis=static_cast<CPseudoOSD*>(pcs->lpCreateParams);

			pThis->m_hwnd=hwnd;
			::SetWindowLongPtr(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(pThis));
		}
		return 0;

	case WM_SIZE:
		{
			CPseudoOSD *pThis=GetThis(hwnd);

			if (pThis->m_fLayeredWindow && ::IsWindowVisible(hwnd))
				pThis->UpdateLayeredWindow();
		}
		return 0;

	case WM_PAINT:
		{
			CPseudoOSD *pThis=GetThis(hwnd);
			PAINTSTRUCT ps;

			::BeginPaint(hwnd,&ps);
			if (!pThis->m_fLayeredWindow)
				pThis->Draw(ps.hdc,ps.rcPaint);
			::EndPaint(hwnd,&ps);
		}
		return 0;

	case WM_TIMER:
		{
			CPseudoOSD *pThis=GetThis(hwnd);

			switch (wParam) {
			case TIMER_ID_HIDE:
				pThis->Hide();
				::KillTimer(hwnd,TIMER_ID_HIDE);
				pThis->m_TimerID&=~TIMER_ID_HIDE;
				if ((pThis->m_TimerID&TIMER_ID_ANIMATION)!=0) {
					::KillTimer(hwnd,TIMER_ID_ANIMATION);
					pThis->m_TimerID&=~TIMER_ID_ANIMATION;
				}
				break;

			case TIMER_ID_ANIMATION:
				pThis->m_AnimationCount++;
				if (pThis->m_fLayeredWindow) {
					RECT rc;

					::GetWindowRect(hwnd,&rc);
					::SetWindowPos(hwnd,NULL,rc.left,rc.top,
								   pThis->m_Position.GetEntireWidth()*(pThis->m_AnimationCount+1)/ANIMATION_FRAMES,
								   pThis->m_Position.Height,
								   SWP_NOZORDER | SWP_NOACTIVATE);
				} else {
					::MoveWindow(hwnd,pThis->m_Position.Left,pThis->m_Position.Top,
								 pThis->m_Position.GetEntireWidth()*(pThis->m_AnimationCount+1)/ANIMATION_FRAMES,
								 pThis->m_Position.Height,
								 TRUE);
				}
				::UpdateWindow(hwnd);
				if (pThis->m_AnimationCount+1==ANIMATION_FRAMES) {
					::KillTimer(hwnd,TIMER_ID_ANIMATION);
					pThis->m_TimerID&=~TIMER_ID_ANIMATION;
				}
				break;
			}
		}
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_MOUSEMOVE:
		{
			CPseudoOSD *pThis=GetThis(hwnd);
			POINT pt;
			RECT rc;

			pt.x=GET_X_LPARAM(lParam);
			pt.y=GET_Y_LPARAM(lParam);
			::MapWindowPoints(hwnd,pThis->m_hwndParent,&pt,1);
			::GetClientRect(pThis->m_hwndParent,&rc);
			if (::PtInRect(&rc,pt))
				return ::SendMessage(pThis->m_hwndParent,uMsg,wParam,MAKELPARAM(pt.x,pt.y));
		}
		return 0;

	case WM_SETCURSOR:
		{
			CPseudoOSD *pThis=GetThis(hwnd);

			return ::SendMessage(pThis->m_hwndParent,uMsg,wParam,lParam);
		}

	case WM_DESTROY:
		{
			CPseudoOSD *pThis=GetThis(hwnd);

			pThis->m_hwnd=NULL;
		}
		return 0;
	}
	return DefWindowProc(hwnd,uMsg,wParam,lParam);
}
