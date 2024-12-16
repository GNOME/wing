@echo on
:: vcvarsall.bat sets various env vars like PATH, INCLUDE, LIB, LIBPATH for the
:: specified build architecture
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
@echo on

:: FIXME: make warnings fatal
pip3 install --upgrade --user meson==1.6.0  || goto :error
meson setup _build || goto :error
ninja -C _build || goto :error

meson test -C _build --timeout-multiplier %MESON_TEST_TIMEOUT_MULTIPLIER% --suite wing || goto :error

:: FIXME: can we get code coverage support?

goto :EOF
:error
exit /b 1
