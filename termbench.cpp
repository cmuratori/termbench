#define VERSION_NAME "TermMarkV2"

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


#if (defined(__APPLE__)) && (defined(__arm64__))
	#include <sys/sysctl.h>
#else
	#include <cpuid.h>  // For __cpuid_count()
#endif



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
    int Result = snprintf(Buffer->Data + Buffer->Count,
                          Buffer->MaxCount - Buffer->Count,
                          "%.04f", Value);
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

static char TerminalBuffer[64*1024*1024];

#include "fast_pipe.h"

struct test_context
{
    int OutputHandle;

    buffer Frame;
    int Width;
    int Height;
    size_t TestCount;

    size_t TotalWriteCount;
    double SecondsElapsed;

    u64 StartTime;
    u64 EndTime;
};

static void RawFlushBuffer(int OutputHandle, buffer *Frame)
{
    WRITE_FUNCTION(OutputHandle, Frame->Data, Frame->Count);
    Frame->Count = 0;
}

static void FlushBuffer(test_context *Context, buffer *Frame)
{
    Context->TotalWriteCount += Frame->Count;
    RawFlushBuffer(Context->OutputHandle, Frame);
}

static void BeginTestTimer(test_context *Context)
{
    Context->StartTime = GetTimer();
}

static void EndTestTimer(test_context *Context)
{
    Context->EndTime = GetTimer();
}

static void FGPerChar(test_context *Context)
{
    buffer Frame = Context->Frame;

    BeginTestTimer(Context);
    for(int FrameIndex = 0; FrameIndex < Context->TestCount; ++FrameIndex)
    {
        for(int Y = 0; Y <= Context->Height; ++Y)
        {
            AppendGoto(&Frame, 1, 1 + Y);
            for(int X = 0; X <= Context->Width; ++X)
            {
                int ForeRed = FrameIndex;
                int ForeGreen = FrameIndex + Y;
                int ForeBlue = FrameIndex + Y + X;

                AppendColor(&Frame, true, ForeRed, ForeGreen, ForeBlue);

                char Char = 'a' + (char)((FrameIndex + X + Y) % ('z' - 'a'));
                AppendChar(&Frame, Char);
            }
        }

        FlushBuffer(Context, &Frame);
    }
    EndTestTimer(Context);
}

static void FGBGPerChar(test_context *Context)
{
    buffer Frame = Context->Frame;

    BeginTestTimer(Context);
    for(int FrameIndex = 0; FrameIndex < Context->TestCount; ++FrameIndex)
    {
        for(int Y = 0; Y <= Context->Height; ++Y)
        {
            AppendGoto(&Frame, 1, 1 + Y);
            for(int X = 0; X <= Context->Width; ++X)
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

        FlushBuffer(Context, &Frame);
    }
    EndTestTimer(Context);
}

static void ManyLine(test_context *Context)
{
    buffer Frame = Context->Frame;

    int TotalCharCount = 27;
    for(size_t At = 0; At < Frame.MaxCount; ++At)
    {
        char Pick = (char)(rand()%TotalCharCount);
        Frame.Data[At] = 'a' + Pick;
        if(Pick == 26) Frame.Data[At] = '\n';
    }

    BeginTestTimer(Context);
    while(Context->TotalWriteCount < Context->TestCount)
    {
        Frame.Count = Frame.MaxCount;
        FlushBuffer(Context, &Frame);
    }
    EndTestTimer(Context);
}

static void LongLine(test_context *Context)
{
    buffer Frame = Context->Frame;

    int TotalCharCount = 26;
    for(size_t At = 0; At < Frame.MaxCount; ++At)
    {
        char Pick = (char)(rand()%TotalCharCount);
        Frame.Data[At] = 'a' + Pick;
    }

    BeginTestTimer(Context);
    while(Context->TotalWriteCount < Context->TestCount)
    {
        Frame.Count = Frame.MaxCount;
        FlushBuffer(Context, &Frame);
    }
    EndTestTimer(Context);
}

typedef void test_function(test_context *Test);
enum test_size
{
    TestSize_Small,
    TestSize_Normal,
    TestSize_Large,

    TestSize_Count,
};
static const char *SizeName[] = {"Small", "Normal", "Large"};

struct test
{
    char const *Name;
    test_function *Function;
    size_t TestCount[TestSize_Count];
};

#define Meg (1024ull*1024ull)
#define Gig (1024ull*Meg)

static test Tests[] =
{
    {"ManyLine", ManyLine, {1*Meg, 1*Gig, 16*Gig}},
    {"LongLine", LongLine, {1*Meg, 1*Gig, 16*Gig}},
    {"FGPerChar", FGPerChar, {512, 8192, 65536}},
    {"FGBGPerChar", FGBGPerChar, {512, 8192, 65536}},
};

int main(int ArgCount, char **Args)
{
    int BypassConhost = USE_FAST_PIPE_IF_AVAILABLE();
    int VirtualTerminalSupport = 0;
    int TestSize = TestSize_Normal;

    for(int ArgIndex = 1; ArgIndex < ArgCount; ++ArgIndex)
    {
        char *Arg = Args[ArgIndex];
        if(strcmp(Arg, "normal") == 0)
        {
            TestSize = TestSize_Normal;
        }
        else if(strcmp(Arg, "small") == 0)
        {
            TestSize = TestSize_Small;
        }
        else if(strcmp(Arg, "large") == 0)
        {
            TestSize = TestSize_Large;
        }
        else
        {
            fprintf(stderr, "Unrecognized argument \"%s\".\n", Arg);
        }
    }

    char CPU[65] = {};

#if (defined(__APPLE__)) && (defined(__arm64__))
    size_t size=64;
    char ARMCPU[65] = {};
    int64_t totalcpu = 0;
    int64_t cpuhigh = 0;
    int64_t cpulow = 0;

    memset(ARMCPU,0,size);
    size=sizeof(totalcpu);
    sysctlbyname("hw.ncpu", &totalcpu, &size, NULL, 0);
    size=sizeof(cpuhigh);
    sysctlbyname("hw.perflevel0.physicalcpu", &cpuhigh, &size, NULL, 0);
    size=sizeof(cpulow);
    sysctlbyname("hw.perflevel1.physicalcpu", &cpulow, &size, NULL, 0);
    size=64;
    sysctlbyname("machdep.cpu.brand_string", &ARMCPU, &size, NULL, 0);
    snprintf(CPU,64, "%s %lld Core Processor (HP:%lld LP:%lld)",ARMCPU,totalcpu,cpuhigh,cpulow);
#else
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

#endif

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

    int Width = 80;
    int Height = 24;

    buffer Frame = {sizeof(TerminalBuffer), 0, TerminalBuffer};

    test_context Contexts[ArrayCount(Tests)] = {};
    for(int TestIndex = 0; TestIndex < ArrayCount(Tests); ++TestIndex)
    {
        test Test = Tests[TestIndex];

        test_context *Context = Contexts + TestIndex;
        Context->OutputHandle = OutputHandle;
        Context->Frame = Frame;
        Context->Width = Width;
        Context->Height = Height;
        Context->TestCount = Test.TestCount[TestSize];

        Test.Function(Context);
        Context->SecondsElapsed = (double)(Context->EndTime - Context->StartTime) / (double)Freq;
    }

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
        test Test = Tests[TestIndex];
        test_context Context = Contexts[TestIndex];

        AppendString(&Frame, Test.Name);
        AppendString(&Frame, ": ");
        AppendDouble(&Frame, Context.SecondsElapsed);
        AppendString(&Frame, "s (");
        AppendDouble(&Frame, GetGBS((double)Context.TotalWriteCount, Context.SecondsElapsed));
        AppendString(&Frame, " GiB/s)\n");

        TotalSeconds += Context.SecondsElapsed;
        TotalBytes += Context.TotalWriteCount;
    }

    AppendString(&Frame, VERSION_NAME);
    AppendString(&Frame, " ");
    AppendString(&Frame, SizeName[TestSize]);
    AppendString(&Frame, ": ");
    AppendDouble(&Frame, TotalSeconds);
    AppendString(&Frame, "s (");
    AppendDouble(&Frame, GetGBS((double)TotalBytes, TotalSeconds));
    AppendString(&Frame, " GiB/s)\n");

    RawFlushBuffer(OutputHandle, &Frame);
}
