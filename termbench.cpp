#define VERSION_NAME "TermMarkV3"

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
typedef unsigned long long u64;

#if _WIN32
#include <windows.h>
#include <intrin.h>
static u64 GetTimerFrequency(void)
{
    LARGE_INTEGER Result;
    QueryPerformanceFrequency(&Result);
    return Result.QuadPart;
}
static u64 GetTimer(void)
{
    LARGE_INTEGER Result;
    QueryPerformanceCounter(&Result);
    return Result.QuadPart;
}
#define WRITE_FUNCTION _write
#else
#include <time.h>
#include <unistd.h>
#include <cpuid.h>
static u64 GetTimerFrequency(void)
{
    u64 Result = 1000000000ull;
    return Result;
}
static u64 GetTimer(void)
{
    struct timespec Spec;
    clock_gettime(CLOCK_MONOTONIC, &Spec);
    u64 Result = ((u64)Spec.tv_sec * 1000000000ull) + (u64)Spec.tv_nsec;
    return Result;
}
#define WRITE_FUNCTION write
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fast_pipe.h"

struct buffer
{
    int MaxCount;
    int Count;
    char *Data;
};

typedef void test_function(buffer *Dest);
struct test
{
    char const *Name;
    test_function *Function;
    double SecondsElapsed;
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
    int Result = snprintf(Buffer->Data + Buffer->Count,
                          Buffer->MaxCount - Buffer->Count,
                          "%.03f", Value);
    if(Result > 0)
    {
        Buffer->Count += Result;
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

static double GetGBS(double Bytes, double Seconds)
{
    double BytesPerGigabyte = 1024.0*1024.0*1024.0;
    double Result = Bytes / (BytesPerGigabyte*Seconds);
    return Result;
}

#define VT_TEST_WIDTH 80
#define VT_TEST_HEIGHT 24

static void FGPerChar( buffer *Dest)
{
    int FrameIndex = 0;
    while(Dest->Count < Dest->MaxCount)
    {
        for(int Y = 0; Y < VT_TEST_HEIGHT; ++Y)
        {
            AppendGoto(Dest, 1, 1 + Y);
            for(int X = 0; X < VT_TEST_WIDTH; ++X)
            {
                int ForeRed = FrameIndex;
                int ForeGreen = FrameIndex + Y;
                int ForeBlue = FrameIndex + Y + X;

                AppendColor(Dest, true, ForeRed, ForeGreen, ForeBlue);

                char Char = 'a' + (char)((FrameIndex + X + Y) % ('z' - 'a'));
                AppendChar(Dest, Char);
            }
        }

        ++FrameIndex;
    }
}

static void FGBGPerChar(buffer *Dest)
{
    int FrameIndex = 0;
    while(Dest->Count < Dest->MaxCount)
    {
        for(int Y = 0; Y < VT_TEST_HEIGHT; ++Y)
        {
            AppendGoto(Dest, 1, 1 + Y);
            for(int X = 0; X < VT_TEST_WIDTH; ++X)
            {
                int BackRed = FrameIndex + Y + X;
                int BackGreen = FrameIndex + Y;
                int BackBlue = FrameIndex;

                int ForeRed = FrameIndex;
                int ForeGreen = FrameIndex + Y;
                int ForeBlue = FrameIndex + Y + X;

                AppendColor(Dest, false, BackRed, BackGreen, BackBlue);
                AppendColor(Dest, true, ForeRed, ForeGreen, ForeBlue);

                char Char = 'a' + (char)((FrameIndex + X + Y) % ('z' - 'a'));
                AppendChar(Dest, Char);
            }
        }
        ++FrameIndex;
    }
}

static void ManyLine(buffer *Dest)
{
    int TotalCharCount = 27;
    while(Dest->Count < Dest->MaxCount)
    {
        char Pick = (char)(rand()%TotalCharCount);
        char Value = 'a' + Pick;
        if(Pick == 26) Value = '\n';
        AppendChar(Dest, Value);
    }
}

static void LongLine(buffer *Dest)
{
    int TotalCharCount = 26;
    while(Dest->Count < Dest->MaxCount)
    {
        char Pick = (char)(rand()%TotalCharCount);
        char  Value = 'a' + Pick;
        AppendChar(Dest, Value);
    }
}

static void Binary(buffer *Dest)
{
    while(Dest->Count < Dest->MaxCount)
    {
        char Value = (rand()%256);
        AppendChar(Dest, Value);
    }
}

#define Meg (1024ull*1024ull)
#define Gig (1024ull*Meg)

static test Tests[] =
{
    {"ManyLine", ManyLine},
    {"LongLine", LongLine},
    {"FGPerChar", FGPerChar},
    {"FGBGPerChar", FGBGPerChar},
    {"Binary", Binary},
};

static char TerminalBuffer[64*1024*1024];

int main(int ArgCount, char **Args)
{
    int BypassConhost = USE_FAST_PIPE_IF_AVAILABLE();
    int VirtualTerminalSupport = 0;
    char const *TestSizeName = "normal";
    u64 TestSize = 1*Gig;

    for(int ArgIndex = 1; ArgIndex < ArgCount; ++ArgIndex)
    {
        char *Arg = Args[ArgIndex];
        if(strcmp(Arg, "small") == 0)
        {
            TestSizeName = Arg;
            TestSize = 1*Meg;
        }
        else if(strcmp(Arg, "large") == 0)
        {
            TestSizeName = Arg;
            TestSize = 16*Gig;
        }
        else
        {
            fprintf(stderr, "Unrecognized argument \"%s\".\n", Arg);
        }
    }

    char CPU[65] = {};
    for(int SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
    {
#if _WIN32
        __cpuid((int *)(CPU + 16*SegmentIndex), 0x80000002 + SegmentIndex);
#else
        __get_cpuid(0x80000002 + SegmentIndex,
                    (int unsigned *)(CPU + 16*SegmentIndex),
                    (int unsigned *)(CPU + 16*SegmentIndex + 4),
                    (int unsigned *)(CPU + 16*SegmentIndex + 8),
                    (int unsigned *)(CPU + 16*SegmentIndex + 12));
#endif
    }

    for(int Num = 0; Num < 256; ++Num)
    {
        buffer NumBuf = {sizeof(NumberTable[Num]), 0, NumberTable[Num]};
        AppendDecimal(&NumBuf, Num);
        AppendChar(&NumBuf, 0);
    }

#if _WIN32
    int OutputHandle = _fileno(stdout);
    _setmode(1, _O_BINARY);

    if(!BypassConhost)
    {
        HANDLE TerminalOut = GetStdHandle(STD_OUTPUT_HANDLE);

        DWORD WinConMode = 0;
        DWORD EnableVirtualTerminalProcessing = 0x0004;
        VirtualTerminalSupport = (GetConsoleMode(TerminalOut, &WinConMode) &&
                                  SetConsoleMode(TerminalOut, (WinConMode & ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT)) |
                                                 EnableVirtualTerminalProcessing));
    }
#else
    int OutputHandle = STDOUT_FILENO;
#endif


    u64 Freq = GetTimerFrequency();

    for(int TestIndex = 0; TestIndex < ArrayCount(Tests); ++TestIndex)
    {
        test *Test = Tests + TestIndex;

        buffer Buffer = {sizeof(TerminalBuffer), 0, TerminalBuffer};
        Test->Function(&Buffer);

        u64 StartTime = GetTimer();
        u64 Remaining = TestSize;
        while(Remaining)
        {
            int unsigned WriteCount = Buffer.Count;
            if(WriteCount > Remaining) WriteCount = (int unsigned)Remaining;

            WRITE_FUNCTION(OutputHandle, Buffer.Data, WriteCount);
            Remaining -= WriteCount;
        }
        u64 EndTime = GetTimer();

        Test->SecondsElapsed = (double)(EndTime - StartTime) / (double)Freq;
    }

    buffer Frame = {sizeof(TerminalBuffer), 0, TerminalBuffer};
    AppendColor(&Frame, 0, 0, 0, 0);
    AppendColor(&Frame, 1, 255, 255, 255);
    AppendString(&Frame, "\x1b[0m");
    for(int ReturnIndex = 0; ReturnIndex < 1024; ++ReturnIndex)
    {
        AppendString(&Frame, "\n");
    }

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
        test *Test = Tests + TestIndex;

        AppendString(&Frame, Test->Name);
        AppendString(&Frame, ": ");
        AppendDouble(&Frame, (double)TestSize / (double)Gig);
        AppendString(&Frame, "gb / ");
        AppendDouble(&Frame, Test->SecondsElapsed);
        AppendString(&Frame, "s (");
        AppendDouble(&Frame, GetGBS((double)TestSize, Test->SecondsElapsed));
        AppendString(&Frame, "gb/s)\n");

        TotalSeconds += Test->SecondsElapsed;
        TotalBytes += TestSize;
    }

    AppendString(&Frame, VERSION_NAME);
    AppendString(&Frame, " ");
    AppendString(&Frame, TestSizeName);
    AppendString(&Frame, ": ");
    AppendDouble(&Frame, TotalSeconds);
    AppendString(&Frame, "s (");
    AppendDouble(&Frame, GetGBS((double)TotalBytes, TotalSeconds));
    AppendString(&Frame, "gb/s)\n");

    WRITE_FUNCTION(OutputHandle, Frame.Data, Frame.Count);
}
