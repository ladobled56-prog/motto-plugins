#!/bin/bash
# Compila la app Standalone de Driftbox en un Mac (doble clic en Finder).
# Requisitos: herramientas de Xcode (se ofrecen solas) y CMake (https://cmake.org/download o "brew install cmake").
set -e
cd "$(dirname "$0")/.."

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Instalando herramientas de desarrollo de Apple... vuelve a abrir este archivo al terminar."
    xcode-select --install
    exit 0
fi
CMAKE=$(command -v cmake || echo "/Applications/CMake.app/Contents/bin/cmake")
if [ ! -x "$CMAKE" ]; then
    echo "Falta CMake. Descárgalo de https://cmake.org/download/ (arrástralo a Aplicaciones) y vuelve a abrir este archivo."
    open "https://cmake.org/download/"
    exit 1
fi

"$CMAKE" -B build-mac -DCMAKE_BUILD_TYPE=Release -DDRIFTBOX_STANDALONE_ONLY=ON -DDRIFTBOX_BUILD_TESTS=OFF
"$CMAKE" --build build-mac --config Release --parallel

APP=build-mac/Driftbox_artefacts/Release/Standalone/Driftbox.app
codesign --force --deep --sign - --preserve-metadata=entitlements,flags "$APP"
mkdir -p dist && rm -rf dist/Driftbox.app && cp -R "$APP" dist/
echo ""
echo "Listo: $(pwd)/dist/Driftbox.app"
open dist
