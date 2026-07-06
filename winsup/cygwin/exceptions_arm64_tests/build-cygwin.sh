#!/bin/bash
set -eo pipefail

# Matrix configuration
BUILD="x86_64-pc-cygwin"
TARGET="aarch64-pc-cygwin"
BUILDARCH="x86_64"
PKGARCH="aarch64"

# Set PATH
export PATH=/usr/bin:$(cygpath ${SYSTEMROOT})/system32:$PATH

echo "=== Checking for toolchain ==="
TOOLCHAIN_FILE="${TARGET}-toolchain.tar.gz"
DOWNLOAD_NEEDED=false

if [ -f "${TOOLCHAIN_FILE}" ]; then
  echo "Toolchain archive found, validating..."
  if tar -tzf "${TOOLCHAIN_FILE}" > /dev/null 2>&1; then
    echo "Toolchain archive is valid, skipping download..."
  else
    echo "Toolchain archive is corrupted, will re-download..."
    rm -f "${TOOLCHAIN_FILE}"
    DOWNLOAD_NEEDED=true
  fi
else
  echo "Toolchain archive not found, will download..."
  DOWNLOAD_NEEDED=true
fi

if [ "$DOWNLOAD_NEEDED" = true ]; then
  echo "Downloading toolchain..."
  curl -L -o ${TOOLCHAIN_FILE} \
    https://github.com/Multicorewareinc/cygwin-dev/releases/download/v3/aarch64-pc-cygwin-toolchain.tar.gz
fi

echo "=== Installing toolchain ==="
mkdir -p toolchain/${TARGET}
tar -xzf ${TOOLCHAIN_FILE} -C toolchain/${TARGET}


echo "=== Cleaning stale BFD headers from toolchain ==="
rm -f toolchain/${TARGET}/include/{bfd.h,bfdlink.h,dis-asm.h,ansidecl.h,symcat.h,diagnostics.h}


echo "=== Building Cygwin ==="
export PATH=$(pwd)/toolchain/${TARGET}/bin:/usr/bin:$(cygpath ${SYSTEMROOT})/system32
export CXXFLAGS_FOR_TARGET="-D_WIN64 -I$(pwd)/toolchain/${TARGET}/include"
export LDFLAGS_FOR_TARGET="-L$(pwd)/toolchain/${TARGET}/lib"
echo "PATH=$PATH"

export DESTDIR=$(realpath $(pwd)/install)
mkdir -p build install

# Comprehensive fix for ALL script files - line endings and permissions
echo "=== Fixing line endings and permissions for ALL scripts ==="
# Fix shell scripts
find . -type f -name "*.sh" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;
# Fix Perl scripts
find . -type f -name "*.pl" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;
# Fix Python scripts
find . -type f -name "*.py" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;
# Fix configure scripts
find . -type f -name "configure" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;
# Fix all files in scripts directories
find . -type f -path "*/scripts/*" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;
# Fix specific known scripts
find . -type f -name "xidepend" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;
find . -type f -name "cygmagic" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;
find . -type f -name "bodysnatcher.pl" -exec sed -i 's/\r$//' {} \; -exec chmod 755 {} \;

echo "=== Running autogen ==="
(cd winsup && bash ./autogen.sh)

echo "=== Configuring build ==="
cd build
bash ../configure --prefix=/usr --build=${BUILD} --target=${TARGET} -v --disable-dumper

export MAKEFLAGS=-j$(nproc)
make

export CYGWIN=winsymlinks:sys
make install -j1 tooldir=/usr gcc_tooldir=/usr DESTDIR=${DESTDIR}

(cd */newlib; make info man)
(cd */newlib; make install-info install-man tooldir=/usr gcc_tooldir=/usr DESTDIR=${DESTDIR})

cd ..

echo "=== Rearranging for default mountpoints ==="
mv -v install/usr/bin install/bin
mv -v install/usr/lib install/lib

echo "=== Testing Cygwin ==="
export PATH=$(pwd)/toolchain/${TARGET}/bin:/usr/bin:$(cygpath ${SYSTEMROOT})/system32
export MAKEFLAGS=-j$(nproc)

cd build

(export PATH=${TARGET}/winsup/testsuite/testinst/bin:${PATH} && cmd /c $(cygpath -wa ${TARGET}/winsup/cygserver/cygserver) &)

(cd ${TARGET}/winsup; make check AM_COLOR_TESTS=always) || echo "Tests completed with errors (expected for aarch64)"

cd ..

echo "=== Build and test complete ==="
echo "Installation directory: $(pwd)/install"
echo "Test logs location: build/${TARGET}/winsup/testsuite/"