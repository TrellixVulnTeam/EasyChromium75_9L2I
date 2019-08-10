@echo on
echo %~d0
echo %cd%

set DEPOT_TOOLS_WIN_TOOLCHAIN=0
set GYP_MSVS_VERSION=2017
set VS2017_INSTALL=D:\Microsoft Visual Studio/2017/Enterprise

if not exist "out_windows" (
  md "out_windows"
)

copy target\windows\args.gn out_windows\args.gn

cd src
call gn gen ../out_windows --ide=vs
call ninja -C ../out_windows chrome
call ninja -C ../out_windows content_shell
cd ..

pause