#define VERSION_NAME "TermMarkV2"

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

static void AppendDecimal(buffer *Buffer, int unsigned Value)
{
    int unsigned Remains = Value;
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

static void AppendDouble(buffer *Buffer, double Value)
{
    AppendDecimal(Buffer, (int unsigned)(Value * 1000.0));
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

static double GetGBS(double Bytes, double Seconds)
{
    double BytesPerGigabyte = 1024.0*1024.0*1024.0;
    double Result = Bytes / (BytesPerGigabyte*Seconds);
    return Result;
}

#define MAX_TERM_WIDTH 4096
#define MAX_TERM_HEIGHT 4096
static char TerminalBuffer[256+16*MAX_TERM_WIDTH*MAX_TERM_HEIGHT];

#include <windows.h>
#include <intrin.h>
#include "fast_pipe.h"

struct test_context
{
    HANDLE TerminalOut;
    buffer Frame;
    int Width;
    int Height;
    size_t TestCount;

    size_t TotalWriteCount;
    double SecondsElapsed;
};

static void RawFlushBuffer(HANDLE TerminalOut, buffer *Frame)
{
    WriteFile(TerminalOut, Frame->Data, Frame->Count, 0, 0);
    Frame->Count = 0;
}

static void FlushBuffer(test_context *Test, buffer *Frame)
{
    Test->TotalWriteCount += Frame->Count;
    RawFlushBuffer(Test->TerminalOut, Frame);
}

static void FGBGPerChar(test_context *Test)
{
    buffer Frame = Test->Frame;

    for(int FrameIndex = 0; FrameIndex < Test->TestCount; ++FrameIndex)
    {
        for(int Y = 0; Y <= Test->Height; ++Y)
        {
            AppendGoto(&Frame, 1, 1 + Y);
            for(int X = 0; X <= Test->Width; ++X)
            {
                int BackRed = FrameIndex + Y + X;
                int BackGreen = FrameIndex + Y;
                int BackBlue = FrameIndex;

                int ForeRed = FrameIndex;
                int ForeGreen = FrameIndex + Y;
                int ForeBlue = FrameIndex + Y + X;

                AppendColor(&Frame, false, BackRed, BackGreen, BackBlue);
                AppendColor(&Frame, true, ForeRed, ForeGreen, ForeBlue);

                char Char = 'a' + (char)((FrameIndex + X + Y) % ('z' - 'a'));
                AppendChar(&Frame, Char);
            }
        }

        FlushBuffer(Test, &Frame);
    }
}

typedef void test_function(test_context *Test);
enum test_size
{
    TestSize_Normal,

    TestSize_Small,
    TestSize_Large,

    TestSize_Count,
};
struct test
{
    char const *Name;
    test_function *Function;
    size_t TestCount[TestSize_Count];
};

static test Tests[] =
{
    {"FGBGPerChar", FGBGPerChar, {64, 1024, 65536}},
};

extern "C" void mainCRTStartup(void)
{
    int BypassConhost = USE_FAST_PIPE_IF_AVAILABLE();
    int VirtualTerminalSupport = 0;
    int TestSize = TestSize_Large;

    char CPU[65] = {};
    for(int SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
    {
        __cpuid((int *)(CPU + 16*SegmentIndex), 0x80000002 + SegmentIndex);
    }

    for(int Num = 0; Num < 256; ++Num)
    {
        buffer NumBuf = {sizeof(NumberTable[Num]), 0, NumberTable[Num]};
        AppendDecimal(&NumBuf, Num);
        AppendChar(&NumBuf, 0);
    }

    HANDLE TerminalOut = GetStdHandle(STD_OUTPUT_HANDLE);

#if _WIN32
    if(!BypassConhost)
    {
        DWORD WinConMode = 0;
        DWORD EnableVirtualTerminalProcessing = 0x0004;
         VirtualTerminalSupport = (GetConsoleMode(TerminalOut, &WinConMode) &&
                                   SetConsoleMode(TerminalOut, (WinConMode & ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT)) |
                                                  EnableVirtualTerminalProcessing));
    }
#endif

    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);

    int Width = 80;
    int Height = 24;

    if(Width > MAX_TERM_WIDTH) Width = MAX_TERM_WIDTH;
    if(Height > MAX_TERM_HEIGHT) Height = MAX_TERM_HEIGHT;

    buffer Frame = {sizeof(TerminalBuffer), 0, TerminalBuffer};

    test_context Contexts[ArrayCount(Tests)] = {};
    for(int TestIndex = 0; TestIndex < ArrayCount(Tests); ++TestIndex)
    {
        test Test = Tests[TestIndex];

        test_context *Context = Contexts + TestIndex;
        Context->TerminalOut = TerminalOut;
        Context->Frame = Frame;
        Context->Width = Width;
        Context->Height = Height;
        Context->TestCount = Test.TestCount[TestSize];

        LARGE_INTEGER Start;
        QueryPerformanceCounter(&Start);
        Test.Function(Context);
        LARGE_INTEGER End;
        QueryPerformanceCounter(&End);

        Context->SecondsElapsed = (double)(End.QuadPart - Start.QuadPart) / (double)Freq.QuadPart;
    }

    AppendGoto(&Frame, 1, 1);
    AppendColor(&Frame, 0, 0, 0, 0);
    AppendColor(&Frame, 1, 255, 255, 255);
    AppendString(&Frame, "\x1b[0m");

    AppendString(&Frame, "CPU: ");
    AppendString(&Frame, CPU);
    AppendString(&Frame, "\n");
    AppendString(&Frame, "VT support: ");
    AppendString(&Frame, VirtualTerminalSupport ? "yes" : "no");
    AppendString(&Frame, "\n");

    double TotalSeconds = 0.0;
    size_t TotalBytes = 0;
    for(int TestIndex = 0; TestIndex < ArrayCount(Tests); ++TestIndex)
    {
        test Test = Tests[TestIndex];
        test_context Context = Contexts[TestIndex];

        AppendString(&Frame, Test.Name);
        AppendString(&Frame, ": ");
        AppendDouble(&Frame, Context.SecondsElapsed);
        AppendString(&Frame, "s (");
        AppendDouble(&Frame, GetGBS((double)Context.TotalWriteCount, Context.SecondsElapsed));
        AppendString(&Frame, "gb/s)\n");

        TotalSeconds += Context.SecondsElapsed;
        TotalBytes += Context.TotalWriteCount;
    }

    AppendString(&Frame, VERSION_NAME);
    AppendString(&Frame, ": ");
    AppendDouble(&Frame, TotalSeconds);
    AppendString(&Frame, "s (");
    AppendDouble(&Frame, GetGBS((double)TotalBytes, TotalSeconds));
    AppendString(&Frame, "gb/s)\n");

    RawFlushBuffer(TerminalOut, &Frame);
}

//
// NOTE(casey): Support definitions for CRT-less Visual Studio and CLANG
//

extern "C" int _fltused = 0;

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
