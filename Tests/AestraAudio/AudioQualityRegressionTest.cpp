#include "Core/AudioGraphState.h"
#include "IO/AudioExportQuantization.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>

using namespace Aestra::Audio;

namespace {

void smoothedParamCompletesLinearRampWithoutBoundarySnap() {
    SmoothedParamD param;
    param.current = 0.0;
    param.setTarget(1.0);
    param.beginRamp(4);

    const double s0 = param.next();
    const double s1 = param.next();
    const double s2 = param.next();
    const double s3 = param.next();

    assert(std::abs(s0 - 0.25) < 1e-12);
    assert(std::abs(s1 - 0.50) < 1e-12);
    assert(std::abs(s2 - 0.75) < 1e-12);
    assert(std::abs(s3 - 1.00) < 1e-12);
    assert(param.samplesRemaining == 0);
    assert(param.current == param.target);

    param.setTarget(0.0);
    param.beginRamp(4);
    assert(std::abs(param.next() - 0.75) < 1e-12);
    assert(std::abs(param.next() - 0.50) < 1e-12);
    assert(std::abs(param.next() - 0.25) < 1e-12);
    assert(std::abs(param.next() - 0.00) < 1e-12);

    std::cout << "PASS: smoothedParamCompletesLinearRampWithoutBoundarySnap\n";
}

void pcm16UsesSymmetricRangeAndClamps() {
    assert(ExportQuantization::quantizePcm16(-1.0f, 0.0) == -32768);
    assert(ExportQuantization::quantizePcm16(1.0f, 0.0) == 32767);
    assert(ExportQuantization::quantizePcm16(0.0f, 0.0) == 0);
    assert(ExportQuantization::quantizePcm16(2.0f, 0.0) == 32767);
    assert(ExportQuantization::quantizePcm16(-2.0f, 0.0) == -32768);
    assert(ExportQuantization::quantizePcm16(NAN, 0.0) == 0);

    std::cout << "PASS: pcm16UsesSymmetricRangeAndClamps\n";
}

void pcm24UsesSymmetricRangeAndPacking() {
    assert(ExportQuantization::quantizePcm24(-1.0f, 0.0) == -8388608);
    assert(ExportQuantization::quantizePcm24(1.0f, 0.0) == 8388607);
    assert(ExportQuantization::quantizePcm24(0.0f, 0.0) == 0);

    uint8_t bytes[3] = {};
    ExportQuantization::storePcm24LittleEndian(-1, bytes);
    assert(bytes[0] == 0xFF);
    assert(bytes[1] == 0xFF);
    assert(bytes[2] == 0xFF);

    ExportQuantization::storePcm24LittleEndian(-8388608, bytes);
    assert(bytes[0] == 0x00);
    assert(bytes[1] == 0x00);
    assert(bytes[2] == 0x80);

    std::cout << "PASS: pcm24UsesSymmetricRangeAndPacking\n";
}

void tpdfDitherIsDeterministicAndNotMonoLockedByHelper() {
    ExportQuantization::TpdfDither a;
    ExportQuantization::TpdfDither b;
    a.setSeed(12345);
    b.setSeed(12345);

    for (int i = 0; i < 16; ++i) {
        assert(a.nextTpdf(ExportQuantization::kPcm16Lsb) == b.nextTpdf(ExportQuantization::kPcm16Lsb));
    }

    ExportQuantization::TpdfDither stereo;
    stereo.setSeed(67890);
    std::set<int16_t> left;
    std::set<int16_t> right;
    for (int i = 0; i < 128; ++i) {
        left.insert(ExportQuantization::quantizePcm16Dithered(0.0f, stereo));
        right.insert(ExportQuantization::quantizePcm16Dithered(0.0f, stereo));
    }

    assert(left.size() > 1);
    assert(right.size() > 1);

    std::cout << "PASS: tpdfDitherIsDeterministicAndNotMonoLockedByHelper\n";
}

} // namespace

int main() {
    smoothedParamCompletesLinearRampWithoutBoundarySnap();
    pcm16UsesSymmetricRangeAndClamps();
    pcm24UsesSymmetricRangeAndPacking();
    tpdfDitherIsDeterministicAndNotMonoLockedByHelper();
    return 0;
}
