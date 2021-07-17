@echo off

set BaseFile=termbench.cpp
set CLLinkFlags=-nodefaultlib -incremental:no -opt:ref -machine:x64 -STACK:0x100000,0x100000 -manifest:no -subsystem:console kernel32.lib user32.lib
set CLCompileFlags=-Zi -Zl -d2Zi+ -Gy -GF -Gs9999999 -GS- -GR- -EHs- -EHc- -EHa- -WX -W4 -nologo -FC -Gm- -diagnostics:column -fp:except- -fp:fast
set CLANGCompileFlags= -D_VC_NODEFAULTLIB=1 -g -fno-autolink -nostdlib -nostdlib++ -mno-stack-arg-probe
set CLANGLinkFlags=-fuse-ld=lld -Wl,-subsystem:console,kernel32.lib,user32.lib

echo -----------------
echo Building debug:
call cl -Fetermbench_debug_msvc.exe -Od %CLCompileFlags% %BaseFile% /link %CLLinkFlags% -RELEASE
call clang++ %CLANGCompileFlags% %CLANGLinkFlags% %BaseFile% -o termbench_debug_clang.exe

echo -----------------
echo Building release:
call cl -Fetermbench_release_msvc.exe -Oi -Oxb2 -O2 %CLCompileFlags% %BaseFile% /link %CLLinkFlags% -RELEASE
call clang++ -O3 %CLANGCompileFlags% %CLANGLinkFlags% %BaseFile% -o termbench_release_clang.exe
