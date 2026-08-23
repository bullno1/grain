if not exist ".build\win\CMakeCache.txt" (
    call cmd\win\prepare.bat
)

if not defined BUILD_TYPE set "BUILD_TYPE=RelWithDebInfo"
cmake --build .build\win --config %BUILD_TYPE% --parallel