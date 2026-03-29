#!/bin/sh

MAJOR=$(grep '#define CF_VERSION_MAJOR' cforge.h | awk '{print $3}')
MINOR=$(grep '#define CF_VERSION_MINOR' cforge.h | awk '{print $3}')
PATCH=$(grep '#define CF_VERSION_PATCH' cforge.h | awk '{print $3}')
MACRO_VER="${MAJOR}.${MINOR}.${PATCH}"

HEADER_VER=$(sed -n 's/.*v\([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' cforge.h | head -1)

if [ "$MACRO_VER" != "$HEADER_VER" ]; then
    echo "Version mismatch: macros say v${MACRO_VER}, header comment says v${HEADER_VER}"
    exit 1
fi

echo "Version OK: v${MACRO_VER}"
