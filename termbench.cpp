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
static buffer GenerateFrame(int FrameIndex, int Width, int Height)
{
    if(Width > MAX_TERM_WIDTH) Width = MAX_TERM_WIDTH;
    if(Height > MAX_TERM_HEIGHT) Height = MAX_TERM_HEIGHT;
    
    buffer Dest = {sizeof(TerminalBuffer), 0, TerminalBuffer};
    
    for(int Y = 0; Y <= Height; ++Y)
    {
        AppendGoto(&Dest, 1, 1 + Y);
        for(int X = 0; X <= Width; ++X)
        {
            int BackRed = FrameIndex + Y + X;
            int BackGreen = FrameIndex + Y;
            int BackBlue = FrameIndex;
            
            int ForeRed = FrameIndex;
            int ForeGreen = FrameIndex + Y;
            int ForeBlue = FrameIndex + Y + X;
            
            char Char = 'a' + (char)(((FrameIndex + X)*Y) % ('z' - 'a'));
            
            AppendColor(&Dest, false, BackRed, BackGreen, BackBlue);
            AppendColor(&Dest, true, ForeRed, ForeGreen, ForeBlue);
            AppendChar(&Dest, Char);
        }
    }
    AppendColor(&Dest, false, 0, 0, 0);
    AppendColor(&Dest, true, 255, 255, 255);
    AppendGoto(&Dest, 1, 1);
    AppendStat(&Dest, "Cells", Width*Height);
    AppendStat(&Dest, "Frame", FrameIndex);
    
    return Dest;
}

#include <windows.h>

extern "C" void mainCRTStartup(void)
{
    for(int Num = 0; Num < 256; ++Num)
    {
        buffer NumBuf = {sizeof(NumberTable[Num]), 0, NumberTable[Num]};
        AppendDecimal(&NumBuf, Num);
        AppendChar(&NumBuf, 0);
    }
    
    HANDLE TerminalIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE TerminalOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    DWORD WinConMode = 0;
    int VirtualTerminalSupport = (GetConsoleMode(TerminalOut, &WinConMode) &&
                                  SetConsoleMode(TerminalOut, (WinConMode & ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT)) |
                                                 ENABLE_VIRTUAL_TERMINAL_PROCESSING));
    
    LARGE_INTEGER Freq;
    QueryPerformanceFrequency(&Freq);
    
    int SizeMS = 0;
    int PrepMS = 0;
    int WriteMS = 0;
    int ReadMS = 0;
    int TotalMS = 0;
    
    int Running = true;
    int FrameIndex = 0;
    while(Running)
    {
        LARGE_INTEGER A;
        QueryPerformanceCounter(&A);
        
        CONSOLE_SCREEN_BUFFER_INFO ConsoleInfo;
        GetConsoleScreenBufferInfo(TerminalOut, &ConsoleInfo);
        int Width = ConsoleInfo.srWindow.Right - ConsoleInfo.srWindow.Left;
        int Height = ConsoleInfo.srWindow.Bottom - ConsoleInfo.srWindow.Top;
        
        LARGE_INTEGER B;
        QueryPerformanceCounter(&B);
        
        buffer Frame = GenerateFrame(FrameIndex++, Width, Height);
        AppendStat(&Frame, "Sizing", SizeMS, "ms");
        AppendStat(&Frame, "Prep", PrepMS, "ms");
        AppendStat(&Frame, "Write", WriteMS, "ms");
        AppendStat(&Frame, "Read", ReadMS, "ms");
        AppendStat(&Frame, "Total", TotalMS, "ms");
        AppendString(&Frame, VirtualTerminalSupport ? "(Win32 vterm)" : "(NO VTERM REPORTED)");
        
        LARGE_INTEGER C;
        QueryPerformanceCounter(&C);
        
        WriteConsoleA(TerminalOut, Frame.Data, Frame.Count, 0, 0);
        
        LARGE_INTEGER D;
        QueryPerformanceCounter(&D);
        
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
                    if(Record.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE)
                    {
                        Running = false;
                    }
                }
            }
        }
        
        LARGE_INTEGER E;
        QueryPerformanceCounter(&E);
        
        SizeMS = GetMS(A.QuadPart, B.QuadPart, Freq.QuadPart);
        PrepMS = GetMS(B.QuadPart, C.QuadPart, Freq.QuadPart);
        WriteMS = GetMS(C.QuadPart, D.QuadPart, Freq.QuadPart);
        ReadMS = GetMS(D.QuadPart, E.QuadPart, Freq.QuadPart);
        TotalMS = GetMS(A.QuadPart, E.QuadPart, Freq.QuadPart);
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
