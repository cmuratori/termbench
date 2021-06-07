# TermBench

This is a simple timing utility you can use to see how slow your terminal program is at parsing escape-sequence-coded color output.  It can be built with either MSVC or CLANG, and has no dependencies.  There is only one file.  Example build commands for MSVC and CLANG are provided in the included build.bat.

# Motiviation

Recently, Microsoft officially "deprecated" using anything other than virtual terminal codes for outputing full-color character displays to the terminal.  Unfortunately, their actual implementation of virtual terminal codes seems excruciatingly slow, and the result is that a full-screen (~30,000 cell) character update runs at around __two frames per second__ on a modern dev machine - which is absurd.

I made this utility in order to test other alternative terminals to see if they were faster, and/or to hopefully motivate someone at Microsoft to figure out why their terminal code is so excruciatingly slow.
