# build_openvdb.ps1 — one-time build of OpenVDB core (+ zlib, c-blosc, oneTBB)
# into extern/install, used by the renderer to read .vdb files (e.g. Houdini
# accretion-disk exports, which are blosc-compressed OpenVDB grids).
#
# Boost is avoided by disabling OPENVDB_USE_DELAYED_LOADING.
# Run from anywhere:  powershell -File scripts\build_openvdb.ps1
$ErrorActionPreference = "Stop"

$root    = Split-Path $PSScriptRoot -Parent
$extern  = Join-Path $root "extern"
$src     = Join-Path $extern "src"
$build   = Join-Path $extern "build"
$prefix  = Join-Path $extern "install"
New-Item -ItemType Directory -Force $src, $build, $prefix | Out-Null

function Fetch($name, $url) {
    $dst = Join-Path $src $name
    if (Test-Path $dst) { Write-Host "[fetch] $name already present"; return $dst }
    $tar = Join-Path $src "$name.tar.gz"
    Write-Host "[fetch] $url"
    Invoke-WebRequest -Uri $url -OutFile $tar -UseBasicParsing
    $tmp = Join-Path $src "_extract_$name"
    New-Item -ItemType Directory -Force $tmp | Out-Null
    tar -xzf $tar -C $tmp
    $inner = Get-ChildItem $tmp -Directory | Select-Object -First 1
    Move-Item $inner.FullName $dst
    Remove-Item $tmp -Recurse -Force
    Remove-Item $tar -Force
    return $dst
}

function Build($name, $srcDir, $cmakeArgs) {
    $b = Join-Path $build $name
    Write-Host "`n==== configuring $name ====" -ForegroundColor Cyan
    # CMAKE_POLICY_VERSION_MINIMUM: several of these projects (and their
    # vendored deps) declare cmake_minimum_required < 3.5, which CMake 4 rejects
    cmake -S $srcDir -B $b -DCMAKE_INSTALL_PREFIX="$prefix" `
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "$name configure failed" }
    Write-Host "==== building $name ====" -ForegroundColor Cyan
    cmake --build $b --config Release --target install --parallel
    if ($LASTEXITCODE -ne 0) { throw "$name build failed" }
}

# ----------------------------------------------------------------- zlib 1.3.1
$zlibSrc = Fetch "zlib" "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz"
Build "zlib" $zlibSrc @("-DZLIB_BUILD_EXAMPLES=OFF")

# -------------------------------------------------------------- c-blosc 1.21.6
$bloscSrc = Fetch "c-blosc" "https://github.com/Blosc/c-blosc/archive/refs/tags/v1.21.6.tar.gz"
Build "c-blosc" $bloscSrc @(
    "-DBUILD_SHARED=OFF", "-DBUILD_STATIC=ON",
    "-DBUILD_TESTS=OFF", "-DBUILD_FUZZERS=OFF", "-DBUILD_BENCHMARKS=OFF")

# -------------------------------------------------------------- oneTBB 2022.0
$tbbSrc = Fetch "oneTBB" "https://github.com/uxlfoundation/oneTBB/archive/refs/tags/v2022.0.0.tar.gz"
Build "oneTBB" $tbbSrc @("-DTBB_TEST=OFF", "-DTBB_EXAMPLES=OFF", "-DTBB_STRICT=OFF")

# ------------------------------------------------------------- OpenVDB 12 core
# Reuse the source FetchContent already downloaded for the NanoVDB headers if
# available, otherwise fetch a fresh copy.
$vdbSrc = Join-Path $root "build\_deps\openvdb-src"
if (-not (Test-Path $vdbSrc)) {
    $vdbSrc = Fetch "openvdb" "https://github.com/AcademySoftwareFoundation/openvdb/archive/refs/tags/v12.0.0.tar.gz"
}
Build "openvdb" $vdbSrc @(
    "-DCMAKE_PREFIX_PATH=$prefix",
    # match the /MD runtime of the consuming app (OpenVDB defaults static
    # builds to /MT, which the linker rejects when mixed)
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
    "-DOPENVDB_BUILD_CORE=ON",
    "-DOPENVDB_CORE_STATIC=ON",
    "-DOPENVDB_CORE_SHARED=OFF",
    "-DOPENVDB_BUILD_BINARIES=OFF",
    "-DOPENVDB_BUILD_VDB_PRINT=OFF",
    "-DOPENVDB_BUILD_UNITTESTS=OFF",
    "-DOPENVDB_BUILD_PYTHON_MODULE=OFF",
    "-DOPENVDB_BUILD_NANOVDB=OFF",
    "-DOPENVDB_USE_DELAYED_LOADING=OFF",   # drops the Boost dependency
    "-DUSE_BLOSC=ON",
    "-DUSE_ZLIB=ON",
    "-DZLIB_USE_STATIC_LIBS=ON",
    "-DUSE_IMATH_HALF=OFF",
    "-DUSE_EXPLICIT_INSTANTIATION=OFF")

Write-Host "`nAll done. Installed into $prefix" -ForegroundColor Green
Get-ChildItem (Join-Path $prefix "lib") -Filter *.lib | Select-Object -ExpandProperty Name
