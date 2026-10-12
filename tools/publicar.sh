#!/bin/sh
# Publica una versión nueva del firmware para que la vitrina la encuentre desde
# el menú 🔧 > Actualización.
#
# Sube la versión en VERSION, compila y deja version.txt + regalo_nico.uf2 en
# la rama "firmware" de GitHub (que solo guarda la última versión).
set -e
cd "$(dirname "$0")/.."

if [ -n "$(git status --porcelain -- . ':!VERSION')" ]; then
    echo "Hay cambios sin commit. Haz commit antes de publicar." >&2
    exit 1
fi

OLD=$(cat VERSION)
N=$((OLD + 1))
echo "$N" > VERSION
trap 'echo "$OLD" > VERSION' EXIT  # si algo falla, VERSION vuelve a como estaba

cmake --build build
picotool info build/regalo_nico.uf2 | grep -q "version: *$N\.0" || {
    echo "El .uf2 no lleva la versión $N" >&2
    exit 1
}

TMP=$(mktemp -d)
cp build/regalo_nico.uf2 "$TMP/"
echo "$N" > "$TMP/version.txt"
git -C "$TMP" init -q -b firmware
git -C "$TMP" add .
git -C "$TMP" -c user.name="$(git config user.name)" -c user.email="$(git config user.email)" \
    commit -q -m "Firmware v$N ($(git rev-parse --short HEAD))"
git -C "$TMP" push -q -f "$(git remote get-url origin)" firmware
rm -rf "$TMP"

git commit -q -m "Publicada la versión $N del firmware" -- VERSION
git push -q
trap - EXIT
echo "Publicada la v$N"
