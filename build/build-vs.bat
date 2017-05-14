set VISUAL_STUDIO_PATH=C:\Program Files (x86)\Microsoft Visual Studio %3.0\VC\

call "%VISUAL_STUDIO_PATH%vcvarsall.bat" amd64
msbuild %1 /maxcpucount:8 /property:Configuration=%2