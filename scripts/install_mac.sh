#!/bin/bash
# Instalación manual de Driftbox en macOS (alternativa al .pkg).
# Uso: abre Terminal en esta carpeta y ejecuta:  ./install_mac.sh
# Instala para tu usuario (no pide contraseña).

set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
APP_DIR="$HOME/Applications"
mkdir -p "$VST3_DIR" "$AU_DIR" "$APP_DIR"

install_bundle () {
    local src="$1" dest="$2"
    if [ -e "$src" ]; then
        rm -rf "$dest/$(basename "$src")"
        cp -R "$src" "$dest/"
        xattr -dr com.apple.quarantine "$dest/$(basename "$src")" 2>/dev/null || true
        echo "  ✓ $(basename "$src") -> $dest"
    fi
}

echo "Instalando Driftbox..."
install_bundle "Driftbox.vst3"      "$VST3_DIR"
install_bundle "Driftbox.component" "$AU_DIR"
install_bundle "Driftbox.app"       "$APP_DIR"

# Fuerza a macOS a re-escanear Audio Units (Logic, GarageBand, Ableton en modo AU)
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Listo. Abre tu DAW y re-escanea los plugins si no aparece Driftbox."
