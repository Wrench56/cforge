#!/bin/sh

CHECK_SCRIPT="tools/check_version.sh"
HEADER_FILE="cforge.h"

OUT="$($CHECK_SCRIPT)" || {
    printf "%s\n" "$OUT"
    printf "Version check failed\n"
    exit 1
}

printf "%s\n" "$OUT"

VERSION=$(printf "%s\n" "$OUT" | sed -n "s/^Version OK: \(v[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)$/\1/p")

[ -n "$VERSION" ] || {
    printf "Error: Could not extract version from check output\n"
    exit 1
}

if ! git diff --quiet || ! git diff --cached --quiet; then
    printf "Warning: Working tree is not clean\n"
fi

if git rev-parse "$VERSION" >/dev/null 2>&1; then
    printf "Error: Tag already exists locally: %s\n" "$VERSION"
    exit 1
fi

if git ls-remote --tags origin "refs/tags/$VERSION" | grep . >/dev/null 2>&1; then
    printf "Error: Tag already exists on origin: %s\n" "$VERSION"
    exit 1
fi

printf "Create release %s? [y/N] " "$VERSION"
read -r reply

case "$reply" in
    y|Y|yes|YES) ;;
    *) printf "Aborted.\n"; exit 0 ;;
esac

git push
git tag -a "$VERSION" -m "Release $VERSION"
git push origin "$VERSION"

gh release create "$VERSION" "$HEADER_FILE" \
    --title "$VERSION" \
    --notes "Release $VERSION"

printf "Release complete: %s.\n" "$VERSION"
