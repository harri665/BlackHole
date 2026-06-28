// make_test_volume — writes a procedural NanoVDB file (density + temperature
// float grids) shaped like a puffy accretion torus, for testing the renderer's
// volumetric path without a Houdini export.
//
// Usage: make_test_volume [out.nvdb]
#include <nanovdb/NanoVDB.h>
#include <nanovdb/tools/GridBuilder.h>
#include <nanovdb/tools/CreateNanoGrid.h>
#include <nanovdb/io/IO.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "test_volume.nvdb";

    // torus parameters in units of M
    const double majorR = 9.0;
    const double minorR = 3.0;
    const double voxel = 0.15;
    const int extent = static_cast<int>((majorR + minorR * 1.5) / voxel);
    const int zExtent = static_cast<int>(minorR * 2.0 / voxel);

    nanovdb::tools::build::Grid<float> density(0.0f, "density");
    nanovdb::tools::build::Grid<float> temperature(0.0f, "temperature");
    density.setTransform(voxel);
    temperature.setTransform(voxel);

    auto dAcc = density.getAccessor();
    auto tAcc = temperature.getAccessor();

    for (int k = -zExtent; k <= zExtent; ++k)
        for (int j = -extent; j <= extent; ++j)
            for (int i = -extent; i <= extent; ++i)
            {
                double x = i * voxel, y = j * voxel, z = k * voxel;
                double rc = std::sqrt(x * x + y * y);
                // distance from the torus center circle
                double dr = rc - majorR;
                double d = std::sqrt(dr * dr + z * z) / minorR;
                if (d >= 1.0)
                    continue;

                double fall = (1.0 - d * d);
                fall *= fall;
                // wispy azimuthal modulation
                double phi = std::atan2(y, x);
                double mod = 0.75 + 0.25 * std::sin(5.0 * phi + 3.0 * dr);

                float dens = static_cast<float>(0.35 * fall * mod);
                if (dens < 1e-4f)
                    continue;
                nanovdb::Coord c(i, j, k);
                dAcc.setValue(c, dens);
                // hotter toward the inner rim and midplane
                double hot = 0.5 + 0.5 * std::max(0.0, -dr / minorR);
                tAcc.setValue(c, static_cast<float>(12000.0 * fall * hot));
            }

    std::vector<nanovdb::GridHandle<>> handles;
    handles.push_back(nanovdb::tools::createNanoGrid(density));
    handles.push_back(nanovdb::tools::createNanoGrid(temperature));
    // template args spelled out: MSVC cannot deduce the VecT template-template
    // parameter from std::vector's allocator argument
    nanovdb::io::writeGrids<nanovdb::HostBuffer, std::vector>(out, handles);

    std::printf("wrote %s (torus: major %.1fM, minor %.1fM, voxel %.2fM)\n",
                out.c_str(), majorR, minorR, voxel);
    return 0;
}
