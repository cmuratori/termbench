#define VERSION_NAME "TermMarkV1"

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

struct buffer
{
    int MaxCount;
    int Count;
    char *Data;
};

static char NumberTable[256][4];

static void AppendChar(buffer *Buffer, char Char)
{
    if(Buffer->Count < Buffer->MaxCount) Buffer->Data[Buffer->Count++] = Char;
}

static void AppendString(buffer *Buffer, char const *String)
{
    while(*String) AppendChar(Buffer, *String++);
}

static void AppendDecimal(buffer *Buffer, int Value)
{
    if(Value < 0)
    {
        AppendChar(Buffer, '-');
        Value = -Value;
    }
    
    int Remains = Value;
    for(int Divisor = 1000000000;
        Divisor > 0;
        Divisor /= 10)
    {
        int Digit = Remains / Divisor;
        Remains -= Digit*Divisor;
        
        if(Digit || (Value != Remains) || (Divisor == 1))
        {
            AppendChar(Buffer, (char)('0' + Digit));
        }
    }
}

static void AppendGoto(buffer *Buffer, int X, int Y)
{
    AppendString(Buffer, "\x1b[");
    AppendDecimal(Buffer, Y);
    AppendString(Buffer, ";");
    AppendDecimal(Buffer, X);
    AppendString(Buffer, "H");
}

static void AppendColor(buffer *Buffer, int IsForeground, int unsigned Red, int unsigned Green, int unsigned Blue)
{
    AppendString(Buffer, IsForeground ? "\x1b[38;2;" : "\x1b[48;2;");
    AppendString(Buffer, NumberTable[Red & 0xff]);
    AppendChar(Buffer, ';');
    AppendString(Buffer, NumberTable[Green & 0xff]);
    AppendChar(Buffer, ';');
    AppendString(Buffer, NumberTable[Blue & 0xff]);
    AppendChar(Buffer, 'm');
}

static int GetMS(int long long Start, int long long End, int long long Frequency)
{
    int Result = (int)(1000*(End - Start) / Frequency);
    return Result;
}

static void AppendStat(buffer *Buffer, char const *Name, int Value, char const *Suffix = "")
{
    AppendString(Buffer, Name);
    AppendString(Buffer, ": ");
    AppendDecimal(Buffer, Value);
    AppendString(Buffer, Suffix);
    AppendString(Buffer, "  ");
}

#define MAX_TERM_WIDTH 4096
#define MAX_TERM_HEIGHT 4096
static char TerminalBuffer[256+16*MAX_TERM_WIDTH*MAX_TERM_HEIGHT];

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include "stdio.h"
#include "unistd.h"
#include "cpuid.h"
#include <sys/time.h>
#include <sys/ioctl.h>
typedef unsigned long DWORD;
typedef unsigned long LONG;
typedef unsigned long long LONGLONG;

typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG  HighPart;
  } DUMMYSTRUCTNAME;
  struct {
    DWORD LowPart;
    LONG  HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER;

void QueryPerformanceFrequency(LARGE_INTEGER* li) {
    LARGE_INTEGER perf;
    perf.QuadPart = 1000;
    *li = perf;
}

void QueryPerformanceCounter(LARGE_INTEGER* li) {
    struct timeval t;
    gettimeofday(&t, nullptr);

    LARGE_INTEGER perf;
    perf.QuadPart = t.tv_usec;
    *li = perf;
}
#endif

#ifdef _WIN32
extern "C" void mainCRTStartup(void)
#else
int main()
#endif
{
    char CPU[65] = {};
#ifdef _WIN32
    for(int SegmentIndex = 0;
        SegmentIndex < 3;
        ++SegmentIndex)
    {
        __cpuid((int *)(CPU + 16*SegmentIndex), 0x80000002 + SegmentIndex);
    }
#endif
    for(int Num = 0; Num < 256; ++Num)
    {
        buffer NumBuf = {sizeof(NumberTable[Num]), 0, NumberTable[Num]};
        AppendDecimal(&NumBuf, Num);
        AppendChar(&NumBuf, 0);
    }
    
#ifdef _WIN32
    HANDLE TerminalIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE TerminalOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    DWORD WinConMode = 0;
    DWORD EnableVirtualTerminalProcessing = 0x0004;
    int VirtualTerminalSupport = (GetConsoleMode(TerminalOut, &WinConMode) &&
                                  SetConsoleMode(TerminalOut, (WinConMode & ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT)) |
                                                 EnableVirtualTerminalProcessing));
#else
    int VirtualTerminalSupport = 1;
#endif
    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);

    int PrepMS = 0;
    int WriteMS = 0;
    int ReadMS = 0;
    int TotalMS = 0;
    
    int Running = true;
    int FrameIndex = 0;
    int DimIsKnown = false;
    
    int WritePerLine = false;
    int ColorPerFrame = false;
    
    int Width = 0;
    int Height = 0;
    
    int ByteCount = 0;
    int TermMark = 0;
    int StatPercent = 0;
    
    LARGE_INTEGER AverageMark = {};
    int long long TermMarkAccum = 0;
    
    while(Running)
    {
        int NextByteCount = 0;
        
        LARGE_INTEGER A;
        QueryPerformanceCounter(&A);
        
        if(TermMarkAccum == 0)
        {
            AverageMark = A;
        }
        else
        {
            int long long AvgMS = 1000*(A.QuadPart - AverageMark.QuadPart) / Freq.QuadPart;
            int long long StatMS = 10000;
            if(AvgMS > StatMS)
            {
                TermMark = (int)(1000*(TermMarkAccum / 1024) / AvgMS);
                AverageMark = A;
                TermMarkAccum = 0;
            }
            
            StatPercent = (int)(100*AvgMS / StatMS);
        }
        
        if(!DimIsKnown)
        {
        #ifdef _WIN32
            CONSOLE_SCREEN_BUFFER_INFO ConsoleInfo;
            GetConsoleScreenBufferInfo(TerminalOut, &ConsoleInfo);
            Width = ConsoleInfo.srWindow.Right - ConsoleInfo.srWindow.Left;
            Height = ConsoleInfo.srWindow.Bottom - ConsoleInfo.srWindow.Top;
            DimIsKnown = true;
        #else
            struct winsize ws;
            ioctl(1, TIOCGWINSZ, &ws);
            Width = ws.ws_col;
            Height = ws.ws_row;
            DimIsKnown = true;
        #endif
        }
        
        if(Width > MAX_TERM_WIDTH) Width = MAX_TERM_WIDTH;
        if(Height > MAX_TERM_HEIGHT) Height = MAX_TERM_HEIGHT;
        
        buffer Frame = {sizeof(TerminalBuffer), 0, TerminalBuffer};
        
        for(int Y = 0; Y <= Height; ++Y)
        {
            AppendGoto(&Frame, 1, 1 + Y);
            for(int X = 0; X <= Width; ++X)
            {
                if(!ColorPerFrame)
                {
                    int BackRed = FrameIndex + Y + X;
                    int BackGreen = FrameIndex + Y;
                    int BackBlue = FrameIndex;
                    
                    int ForeRed = FrameIndex;
                    int ForeGreen = FrameIndex + Y;
                    int ForeBlue = FrameIndex + Y + X;
                    
                    AppendColor(&Frame, false, BackRed, BackGreen, BackBlue);
                    AppendColor(&Frame, true, ForeRed, ForeGreen, ForeBlue);
                }
                
                char Char = 'a' + (char)((FrameIndex + X + Y) % ('z' - 'a'));
                AppendChar(&Frame, Char);
            }
        
            if(WritePerLine)
            {
                NextByteCount += Frame.Count;
                #ifdef _WIN32
                WriteConsoleA(TerminalOut, Frame.Data, Frame.Count, 0, 0);
                #else
                write(STDOUT_FILENO, Frame.Data, Frame.Count);
                #endif
                Frame.Count = 0;
            }
        }
        
        AppendColor(&Frame, false, 0, 0, 0);
        AppendColor(&Frame, true, 255, 255, 255);
        AppendGoto(&Frame, 1, 1);
        AppendStat(&Frame, "Glyphs", (Width*Height) / 1024, "k");
        AppendStat(&Frame, "Bytes", ByteCount / 1024, "kb");
        AppendStat(&Frame, "Frame", FrameIndex);

        if(!WritePerLine) AppendStat(&Frame, "Prep", PrepMS, "ms");
        AppendStat(&Frame, "Write", WriteMS, "ms");
        AppendStat(&Frame, "Read", ReadMS, "ms");
        AppendStat(&Frame, "Total", TotalMS, "ms");
        
        AppendGoto(&Frame, 1, 2);
        AppendString(&Frame, WritePerLine ? "[F1]:write per line " : "[F1]:write per frame ");
        AppendString(&Frame, ColorPerFrame ? "[F2]:color per frame " : "[F2]:color per char ");
        
        if(!WritePerLine)
        {
            AppendGoto(&Frame, 1, 3);
            if(TermMark)
            {
                AppendStat(&Frame, VERSION_NAME, TermMark, ColorPerFrame ? "kg/s" : "kcg/s");
                AppendString(&Frame, "(");
                AppendString(&Frame, CPU);
                AppendString(&Frame, " Win32 ");
                AppendString(&Frame, VirtualTerminalSupport ? "VTS)" : "NO VTS REPORTED)");
            }
            else
            {
                AppendStat(&Frame, "(collecting", StatPercent, "%)");
            }
        }
        
        LARGE_INTEGER B;
        QueryPerformanceCounter(&B);
        
        NextByteCount += Frame.Count;
        #ifdef _WIN32
        WriteConsoleA(TerminalOut, Frame.Data, Frame.Count, 0, 0);
        #else
        write(STDOUT_FILENO, Frame.Data, Frame.Count);
        #endif        
        
        LARGE_INTEGER C;
        QueryPerformanceCounter(&C);

        int ResetStats = false;
        #ifdef _WIN32
        while(WaitForSingleObject(TerminalIn, 0) == WAIT_OBJECT_0)
        {
            INPUT_RECORD Record;
            DWORD RecordCount = 0;
            ReadConsoleInput(TerminalIn, &Record, 1, &RecordCount);
            if(RecordCount)
            {
                if((Record.EventType == KEY_EVENT) &&
                   (Record.Event.KeyEvent.bKeyDown) &&
                   (Record.Event.KeyEvent.wRepeatCount == 1))
                {
                    switch(Record.Event.KeyEvent.wVirtualKeyCode)
                    {
                        case VK_ESCAPE: Running = false; break;
                        case VK_F1:
                        {
                            WritePerLine = !WritePerLine;
                            ResetStats = true;
                        } break;
                        
                        case VK_F2:
                        {
                            ColorPerFrame = !ColorPerFrame;
                            ResetStats = true;
                        } break;
                    }
                }
                else if(Record.EventType == WINDOW_BUFFER_SIZE_EVENT)
                {
                    DimIsKnown = false;
                    ResetStats = true;
                }
            }
        }
        #endif

        LARGE_INTEGER D;
        QueryPerformanceCounter(&D);
        
        PrepMS = GetMS(A.QuadPart, B.QuadPart, Freq.QuadPart);
        WriteMS = GetMS(B.QuadPart, C.QuadPart, Freq.QuadPart);
        ReadMS = GetMS(C.QuadPart, D.QuadPart, Freq.QuadPart);
        TotalMS = GetMS(A.QuadPart, D.QuadPart, Freq.QuadPart);
        ByteCount = NextByteCount;
        ++FrameIndex;
        
        if(ResetStats)
        {
            TermMarkAccum = TermMark = 0;
        }
        else
        {
            TermMarkAccum += Width*Height;
        }
    }
}

//
// NOTE(casey): Support definitions for CRT-less Visual Studio and CLANG
//

#ifndef __clang__
#undef function
#pragma function(memset)
#endif
extern "C" void *memset(void *DestInit, int Source, size_t Size)
{
    unsigned char *Dest = (unsigned char *)DestInit;
    while(Size--) *Dest++ = (unsigned char)Source;
    
    return(DestInit);
}
    
#ifndef __clang__
#pragma function(memcpy)
#endif
extern "C" void *memcpy(void *DestInit, void const *SourceInit, size_t Size)
{
    unsigned char *Source = (unsigned char *)SourceInit;
    unsigned char *Dest = (unsigned char *)DestInit;
    while(Size--) *Dest++ = *Source++;
    
    return(DestInit);
}
