#include "doctest.h"
#include "../src/ThrGenerator.h"
#include <vector>
#include <cmath>

TEST_CASE("ThrGenerator::generate_thr") {
    int width = 100;
    int height = 100;
    // Center 50,50
    
    // Max radius will be distance to 100,50 -> 50.
    std::vector<Point> points = {
        {100, 50}, // Right edge -> Theta 0 (or near), Rho 1.0
        {50, 0}    // Top edge -> Y is 0 (inverted?) In image coords 0 is top.
                   // dx = 0, dy = -50. atan2(50, 0) = PI/2.
                   // Logic in code: dy = p.y - center.y = -50. atan2(-dy, dx) = atan2(50, 0) = PI/2.
    };

    auto thr = ThrGenerator::generate_thr(points, width, height);

    // Should include start/end zero points + 2 points
    REQUIRE(thr.size() == 4);

    // Start point (center)
    CHECK(thr[0].rho == 0.0);

    // First point {100, 50}
    // dx=50, dy=0. atan2(0, 50) = 0.
    CHECK(thr[1].theta == doctest::Approx(0.0));
    CHECK(thr[1].rho == doctest::Approx(1.0));

    // Second point {50, 0}
    // dx=0, dy=-50. atan2(dy, dx) = atan2(-50, 0) = -PI/2 = -1.5707...
    CHECK(thr[2].theta == doctest::Approx(-1.570796).epsilon(0.001));
    CHECK(thr[2].rho == doctest::Approx(1.0));

    // End point (center)
    CHECK(thr[3].rho == 0.0);
}

TEST_CASE("ThrGenerator clamps Cartesian output to the canvas") {
    const std::vector<ThrPoint> polar = {{0.0, 2.0}, {3.141592653589793, -1.0}};
    const auto points = ThrGenerator::to_cartesian(polar, 100, 80);

    REQUIRE(points.size() == 2);
    CHECK(points[0].x == 90);
    CHECK(points[0].y == 40);
    CHECK(points[1].x == 50);
    CHECK(points[1].y == 40);
    CHECK(ThrGenerator::to_cartesian(polar, 0, 80).empty());
}

#include "../src/ThrPreview.h"
#include "stb_image.h"
#include <cstdio>
#include <limits>

TEST_CASE("THR parser matches firmware separators and rejects corrupt complete files") {
    const auto points=ThrGenerator::parse("  # comment\n\t// comment\n0, 0.5 # middle\n6.28\t1 // end\n");
    REQUIRE(points.size()==2);
    CHECK(points[0].rho==doctest::Approx(.5));
    for(const std::string s:{"0 1.01", "0 -0.1", "0 nan", "inf 1", "1x 0.2", "0 1 extra", "0 0\nnope\n1 1", "0 1e999"})
        CHECK_THROWS(ThrGenerator::parse(s));
    CHECK_THROWS(ThrGenerator::parse(std::string("0 0\0ignored",11)));
    CHECK_THROWS(ThrGenerator::parse(std::string(128,' ')));
}

TEST_CASE("Preview preserves full turns and floating pixel positions") {
    constexpr double pi=3.14159265358979323846;
    const auto path=ThrPreview::path({{0,.5},{4*pi,.5}},800);
    REQUIRE(path.size()>1000);
    double rotation=0;
    for(size_t i=1;i<path.size();++i) {
        const auto a=path[i-1],b=path[i];
        CHECK(std::hypot(b.x-400,b.y-400)==doctest::Approx(190).epsilon(1e-10));
        rotation+=std::atan2((a.x-400)*(b.y-400)-(a.y-400)*(b.x-400),
                            (a.x-400)*(b.x-400)+(a.y-400)*(b.y-400));
    }
    CHECK(rotation==doctest::Approx(4*pi));
    const auto single=ThrPreview::path({{.123,.456}},800);
    CHECK(single[0].x==doctest::Approx(400+380*.456*std::cos(.123)).epsilon(1e-12));
    CHECK(single[0].y==doctest::Approx(400+380*.456*std::sin(.123)).epsilon(1e-12));
    CHECK_THROWS(ThrPreview::path({{0,1},{1e12,1}},800));
    CHECK_THROWS(ThrPreview::path({{0,2}},800));
    CHECK_THROWS(ThrPreview::path({},800));
}

TEST_CASE("PNG covers the complete circular arc with unbiased center and margins") {
    constexpr double pi=3.14159265358979323846;
    for(int size:{128,800}) {
        const std::string filename="preview-regression-"+std::to_string(size)+".png";
        REQUIRE(ThrPreview::png({{0,1},{2*pi,1}},filename,size));
        int w,h,c;auto* pixels=stbi_load(filename.c_str(),&w,&h,&c,4);
        REQUIRE(pixels!=nullptr);CHECK(w==size);CHECK(h==size);
        double mass=0,xmass=0,ymass=0;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const int alpha=pixels[(y*w+x)*4+3];mass+=alpha;xmass+=(x+.5)*alpha;ymass+=(y+.5)*alpha;
        }
        REQUIRE(mass>0);CHECK(xmass/mass==doctest::Approx(size/2.0).epsilon(.0001));
        CHECK(ymass/mass==doctest::Approx(size/2.0).epsilon(.0001));
        for(int k=0;k<4;++k) {
            const int x=std::lround(size/2.0+size*.475*std::cos(k*pi/2));
            const int y=std::lround(size/2.0+size*.475*std::sin(k*pi/2));
            CHECK(pixels[(y*w+x)*4+3]>40);
        }
        CHECK(pixels[((size/2)*w+size/2)*4+3]==0);
        stbi_image_free(pixels);std::remove(filename.c_str());
    }
}

TEST_CASE("GIF renderer handles single points and reports write failures") {
    REQUIRE(ThrPreview::gif({{0,.5}},"preview-regression.gif",32));
    std::remove("preview-regression.gif");
    CHECK_FALSE(ThrPreview::png({{0,0}},"/nonexistent-thr-preview/out.png"));
    CHECK_FALSE(ThrPreview::gif({{0,0}},"/nonexistent-thr-preview/out.gif",32));
}
