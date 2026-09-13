@echo off

clang++ -DGRAPHICS_API_D3D12 texture_loader.cpp obj_loader.cpp source/backend/gpu/d3d12.graphics.cpp source/backend/gpu/gpu.cpp source/core/debug.cpp source/backend/input/windows.key.cpp source/backend/input/mouse.cpp source/backend/input/keyboard.cpp source/backend/window/config.cpp source/backend/window/windows.cpp main.cpp -o main.exe -Isource -ld3d12 -ld3dcompiler -ldxgi -static-libgcc -luser32 -lkernel32

pause