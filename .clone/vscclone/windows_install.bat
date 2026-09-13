@echo off

mkdir extension
xcopy extension.js extension
xcopy package.json extension

7z a -tzip -sdel vscclone.vsix extension
code --install-extension vscclone.vsix

pause