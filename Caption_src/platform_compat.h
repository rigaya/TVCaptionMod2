#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#ifndef _WIN32
  // Linux向けの定義
  #include <stdint.h>
  #include "rgy_osdep.h"
  #include "rgy_util.h"
  #include "rgy_tchar.h"
  
  // wstring_to_stringを使ってWideCharToMultiByteを再現する
  static inline int WideCharToMultiByte(int codePage, DWORD flags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar) {
    std::string str = wstring_to_string(lpWideCharStr, codePage);
    strcpy(lpMultiByteStr, str.c_str());
    return str.length();
  }

  // char_to_wstringを使ってMultiByteToWideCharを再現する
  static inline int MultiByteToWideChar(int codePage, DWORD flags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar) {
    std::wstring wstr = char_to_wstring(lpMultiByteStr, codePage);
    wcscpy(lpWideCharStr, wstr.c_str());
    return wstr.length() * sizeof(wchar_t); // バイト数を返す
  }

  static inline void OutputDebugString(LPCSTR lpOutputString) {
    fprintf(stderr, "%s", lpOutputString);
  }

  static inline void OutputDebugString(LPCWSTR lpOutputString) {
    fwprintf(stderr, L"%s", lpOutputString);
  }

  // WINAPIの定義 (Linuxでは空の定義)
  #define WINAPI
  
  // CAPTION_DLLEXPORTの再定義 (Linuxでは空)
  #ifdef CAPTION_EXPORTS
    #define CAPTION_DLLEXPORT
  #else
    #define CAPTION_DLLEXPORT
  #endif
  
  // その他必要なWindows定義
  #define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
  #define MAX_PATH 260

    // TEXT マクロの問題を解決する
  #ifdef TEXT
    #undef TEXT
  #endif
  
  // 文字列リテラルを連結できるようにTEXTマクロを再定義
  #define TEXT(str) str
#endif


#endif // PLATFORM_COMPAT_H