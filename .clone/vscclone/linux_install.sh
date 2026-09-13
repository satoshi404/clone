#!/bin /bash

mkdir extension
cp -R extension.js extension
cp -R package.json extension

zip -rqm vscclone.vsix extension
codebin --install-extension vscclone.vsix

rem -rf extension