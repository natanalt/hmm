#include <chrono>
#include <functional>
#include <iostream>
#include <string>

#include "base.h"
#include "cmdline.h"
#include "heightmap.h"
#include "stl.h"
#include "obj.h"
#include "triangulator.h"
#include "utils.h"

int main(int argc, char **argv) {
    const auto startTime = std::chrono::steady_clock::now();

    // parse command line arguments
    cmdline::parser p;

    // long name, short name (or '\0'), description, mandatory (vs optional),
    // default value, constraint
    p.add<float>("xsize", 'x', "requested size of the mesh in the X axis", true);
    p.add<float>("ysize", 'y', "requested size of the mesh in the Y axis", true);
    p.add<float>("zscale", 'z', "z scale relative to x & y", true);
    p.add<float>("error", 'e', "maximum triangulation error", false, 0.001);
    p.add<int>("triangles", 't', "maximum number of triangles", false, 0);
    p.add<int>("points", 'p', "maximum number of vertices", false, 0);
    p.add<float>("base", 'b', "solid base height", false, 0);
    p.add("level", '\0', "auto level input to full grayscale range");
    p.add("invert", '\0', "invert heightmap");
    p.add<int>("blur", '\0', "gaussian blur sigma", false, 0);
    p.add<float>("gamma", '\0', "gamma curve exponent", false, 0);
    p.add<int>("border-size", '\0', "border size in pixels", false, 0);
    p.add<float>("border-height", '\0', "border z height", false, 1);
    p.add<std::string>("normal-map", '\0', "path to write normal map png", false, "");
    p.add<std::string>("shade-path", '\0', "path to write hillshade png", false, "");
    p.add<float>("shade-alt", '\0', "hillshade light altitude", false, 45);
    p.add<float>("shade-az", '\0', "hillshade light azimuth", false, 0);
    p.add("quiet", 'q', "suppress console output");
    p.footer("infile outfile.[stl/obj]");
    p.parse_check(argc, argv);

    // infile required
    if (p.rest().size() < 1) {
        std::cerr << "infile required" << std::endl << p.usage();
        std::exit(1);
    }

    // extract command line arguments
    const std::string inFile = p.rest()[0];
    const float xSize = p.get<float>("xsize");
    const float ySize = p.get<float>("ysize");
    const float zScale = p.get<float>("zscale");
    const float maxError = p.get<float>("error");
    const int maxTriangles = p.get<int>("triangles");
    const int maxPoints = p.get<int>("points");
    const float baseHeight = p.get<float>("base");
    const bool level = p.exist("level");
    const bool invert = p.exist("invert");
    const int blurSigma = p.get<int>("blur");
    const float gamma = p.get<float>("gamma");
    const int borderSize = p.get<int>("border-size");
    const float borderHeight = p.get<float>("border-height");
    const std::string normalmapPath = p.get<std::string>("normal-map");
    const std::string shadePath = p.get<std::string>("shade-path");
    const float shadeAlt = p.get<float>("shade-alt");
    const float shadeAz = p.get<float>("shade-az");
    const bool quiet = p.exist("quiet");

    const bool hasOutFile = p.rest().size() > 1;
    const bool hasOtherFile = !normalmapPath.empty() || !shadePath.empty();
    if (!hasOutFile && !hasOtherFile) {
        std::cerr << "outfile required" << std::endl << p.usage();
        std::exit(1);
    }

    // helper function to display elapsed time of each step
    const auto timed = [quiet](const std::string &message)
        -> std::function<void()>
    {
        if (quiet) {
            return [](){};
        }
        printf("%s... ", message.c_str());
        fflush(stdout);
        const auto startTime = std::chrono::steady_clock::now();
        return [message, startTime]() {
            const std::chrono::duration<double> elapsed =
                std::chrono::steady_clock::now() - startTime;
            printf("%gs\n", elapsed.count());
        };
    };

    // load heightmap
    auto done = timed("loading heightmap");
    const auto hm = std::make_shared<Heightmap>(inFile);
    done();

    int w = hm->Width();
    int h = hm->Height();
    if (w * h == 0) {
        std::cerr
            << "invalid heightmap file (try png, jpg, etc.)" << std::endl
            << p.usage();
        std::exit(1);
    }

    // display statistics
    if (!quiet) {
        printf("  %d x %d = %d pixels\n", w, h, w * h);
    }

    // auto level heightmap
    if (level) {
        hm->AutoLevel();
    }

    // invert heightmap
    if (invert) {
        hm->Invert();
    }

    // blur heightmap
    if (blurSigma > 0) {
        done = timed("blurring heightmap");
        hm->GaussianBlur(blurSigma);
        done();
    }

    // apply gamma curve
    if (gamma > 0) {
        hm->GammaCurve(gamma);
    }

    // add border
    if (borderSize > 0) {
        hm->AddBorder(borderSize, borderHeight);
    }

    // get updated size
    w = hm->Width();
    h = hm->Height();

    if (hasOutFile) {
        // triangulate
        done = timed("triangulating");
        Triangulator tri(hm);
        tri.Run(maxError, maxTriangles, maxPoints);
        auto points = tri.Points(zScale);
        auto triangles = tri.Triangles();
        done();

        // add base
        if (baseHeight > 0) {
            done = timed("adding solid base");
            const float z = -baseHeight * zScale;
            AddBase(points, triangles, w, h, z);
            done();
        }

        // display statistics
        if (!quiet) {
            const int naiveTriangleCount = (w - 1) * (h - 1) * 2;
            printf("  error = %g\n", tri.Error());
            printf("  points = %ld\n", points.size());
            printf("  triangles = %ld\n", triangles.size());
            printf("  vs. naive = %g%%\n", 100.f * triangles.size() / naiveTriangleCount);
        }

        // HACK(nat): The upstream triangulation algorithm makes the mesh size match 1 heightmap pixel to 1 unit.
        //            In order to allow variable mesh size via --xsize and --ysize parameters, I decided to just adjust
        //            all vertex positions as a post-processing pass for the vertex data.
        done = timed("postprocess rescaling pass");
        const auto posScaleFactor = glm::vec3(xSize / float(w), ySize / float(h), 1.0);
        for (glm::vec3& vertexPos : points) {
            vertexPos *= posScaleFactor;
        }
        done();

        // Generating UVs. The vertex XY coordinates should cleanly map to UVs once normalized due to the heightmap
        // nature of the mesh :3
        done = timed("generating UVs");
        std::vector<glm::vec2> uvs(points.size(), glm::vec2(0.0f));
        const auto uvScaleFactor = glm::vec2(1.0f / xSize, 1.0f / ySize);
        for (int i = 0; i < uvs.size(); i++) {
            const glm::vec3 vertexPos = points[i];
            uvs[i] = glm::vec2(vertexPos.x, vertexPos.y) * uvScaleFactor;
        }
        done();

        // write output file
        const std::string outFile = p.rest()[1];
        
        if (StrEndsWith(outFile.c_str(), ".stl", false)) {
            done = timed("writing .stl output");
            SaveBinarySTL(outFile, points, triangles);
            done();
        } else if (StrEndsWith(outFile.c_str(), ".obj", false)) {
            done = timed("writing .obj output");
            SaveWavefrontOBJ(outFile, points, triangles, uvs);
            done();
        } else {
            fprintf(stderr, "Error: Could not deduce target file format from the output file extension.\n\n");
            fprintf(stderr, "The extension (case-insensitive) should be either:\n");
            fprintf(stderr, "   - .stl for STL files\n");
            fprintf(stderr, "   - .obj for Wavefront .obj files\n");
            return 1;
        }
    }

    // compute normal map
    if (!normalmapPath.empty()) {
        done = timed("computing normal map");
        hm->SaveNormalmap(normalmapPath, zScale);
        done();
    }

    // compute hillshade image
    if (!shadePath.empty()) {
        done = timed("computing hillshade image");
        hm->SaveHillshade(shadePath, zScale, shadeAlt, shadeAz);
        done();
    }

    // show total elapsed time
    if (!quiet) {
        const std::chrono::duration<double> elapsed =
            std::chrono::steady_clock::now() - startTime;
        printf("%gs\n", elapsed.count());
    }

    return 0;
}
