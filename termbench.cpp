#define VERSION_NAME "TermMarkV3"

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
typedef unsigned long long u64;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fast_pipe.h"
#include "termbench_message.h"

struct platform_values
{
    u64 TimerFrequency;

    int OutputHandle;
    int FastPipesEnabled;
    int VTCodesEnabled;

    char CPUString[256];
};

#if _WIN32

#include <windows.h>
#include <intrin.h>

static u64 GetTimer(void)
{
    LARGE_INTEGER Result;
    QueryPerformanceCounter(&Result);
    return Result.QuadPart;
}

static void PreparePlatform(platform_values *Result)
{
    Result->FastPipesEnabled = USE_FAST_PIPE_IF_AVAILABLE();

    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);
    Result->TimerFrequency = Freq.QuadPart;

    Result->OutputHandle = _fileno(stdout);
    _setmode(Result->OutputHandle, _O_BINARY);

    if(Result->FastPipesEnabled)
    {
        Result->VTCodesEnabled = true;
    }
    else
    {
        HANDLE TerminalOut = GetStdHandle(STD_OUTPUT_HANDLE);

        DWORD WinConMode = 0;
        if(GetConsoleMode(TerminalOut, &WinConMode))
        {
            DWORD EnableVirtualTerminalProcessing = 0x0004;
            DWORD NewConMode = (WinConMode & ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT)) |
                EnableVirtualTerminalProcessing;
            if(SetConsoleMode(TerminalOut, NewConMode))
            {
                Result->VTCodesEnabled = true;
            }
        }

        SetConsoleOutputCP(65001);
    }

    for(int SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
    {
        __cpuid((int *)(Result->CPUString + 16*SegmentIndex), 0x80000002 + SegmentIndex);
    }
}

#define WRITE_FUNCTION _write

#else

#include <time.h>
#include <unistd.h>
#if __x86_64__
#include <cpuid.h>
#endif

#if __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

static u64 GetTimer(void)
{
    struct timespec Spec;
    clock_gettime(CLOCK_MONOTONIC, &Spec);
    u64 Result = ((u64)Spec.tv_sec * 1000000000ull) + (u64)Spec.tv_nsec;
    return Result;
}

static void PreparePlatform(platform_values *Result)
{
    Result->TimerFrequency = 1000000000ull;
    Result->OutputHandle = STDOUT_FILENO;
    Result->VTCodesEnabled = true;

#if __x86_64__
    for(int SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
    {
        __get_cpuid(0x80000002 + SegmentIndex,
                    (int unsigned *)(Result->CPUString + 16*SegmentIndex),
                    (int unsigned *)(Result->CPUString + 16*SegmentIndex + 4),
                    (int unsigned *)(Result->CPUString + 16*SegmentIndex + 8),
                    (int unsigned *)(Result->CPUString + 16*SegmentIndex + 12));
    }
#elif __APPLE__
    size_t CpuStringLen = sizeof(Result->CPUString);

    // Writes out a product model and version - mapping this to a specific silicon chip is left as an exercise for the user.
    // However, we know it's Arm ("Apple Silicon"), or we'd have take the above cpuid branch.
    // Example: An M1 MacBookAir reports "MacBookAir10,1"
    sysctlbyname(
                 "hw.model",
                 Result->CPUString,
                 &CpuStringLen,
                 NULL, // nNewVal
                 0     // newlen
                 );
#else
#warning Non-x64 CPU identification code needs to go here.  Please contribute some for your platform!
    strncpy(Result->CPUString, "Unknown", sizeof(Result->CPUString));
#endif
}

#define WRITE_FUNCTION write

#endif

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
    AppendDecimal(Buffer, Red & 0xff);
    AppendChar(Buffer, ';');
    AppendDecimal(Buffer, Green & 0xff);
    AppendChar(Buffer, ';');
    AppendDecimal(Buffer, Blue & 0xff);
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
    platform_values Platform = {};
    PreparePlatform(&Platform);

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

            WRITE_FUNCTION(Platform.OutputHandle, Buffer.Data, WriteCount);
            Remaining -= WriteCount;
        }
        u64 EndTime = GetTimer();

        Test->SecondsElapsed = (double)(EndTime - StartTime) / (double)Platform.TimerFrequency;
    }

    buffer Frame = {sizeof(TerminalBuffer), 0, TerminalBuffer};
    AppendColor(&Frame, 0, 0, 0, 0);
    AppendColor(&Frame, 1, 255, 255, 255);
    AppendString(&Frame, "\x1b[0m");
    for(int ReturnIndex = 0; ReturnIndex < 1024; ++ReturnIndex)
    {
        AppendString(&Frame, "\n");
    }

    AppendString(&Frame, (char *)EndingMessage);
    AppendString(&Frame, "\n");

    AppendString(&Frame, "CPU: ");
    AppendString(&Frame, Platform.CPUString);
    AppendString(&Frame, "\n");

    AppendString(&Frame, "VT support expected: ");
    AppendString(&Frame, Platform.VTCodesEnabled ? "yes" : "no");
    AppendString(&Frame, "\n");

    AppendString(&Frame, "Fast pipes: ");
    AppendString(&Frame, Platform.FastPipesEnabled ? "yes" : "no");
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

    WRITE_FUNCTION(Platform.OutputHandle, Frame.Data, Frame.Count);
}
