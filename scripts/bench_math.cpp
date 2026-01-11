#include <iostream>
#include <cmath>
#include <chrono>
#include <vector>
#include <iomanip>

constexpr double LN10_OVER_20_D = 0.11512925464970228420089957273422;

inline double dbToGain_Pow(double db) {
    if (db <= -90.0) return 0.0;
    return std::pow(10.0, db / 20.0);
}

inline double dbToGain_Exp(double db) {
    if (db <= -90.0) return 0.0;
    return std::exp(db * LN10_OVER_20_D);
}

int main() {
    const int N = 10000000;
    std::vector<double> inputs(N);
    for(int i=0; i<N; ++i) inputs[i] = -90.0 + (i % 1000) * 0.1;

    // Warmup
    {
        double sum = 0;
        for(double d : inputs) sum += dbToGain_Pow(d);
        for(double d : inputs) sum += dbToGain_Exp(d);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double sum1 = 0;
    for(double d : inputs) sum1 += dbToGain_Pow(d);
    auto t2 = std::chrono::high_resolution_clock::now();

    auto t3 = std::chrono::high_resolution_clock::now();
    double sum2 = 0;
    for(double d : inputs) sum2 += dbToGain_Exp(d);
    auto t4 = std::chrono::high_resolution_clock::now();

    double msPow = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t2-t1).count();
    double msExp = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t4-t3).count();

    std::cout << "Pow: " << msPow << " ms\n";
    std::cout << "Exp: " << msExp << " ms\n";
    std::cout << "Speedup: " << msPow / msExp << "x\n";
    std::cout << "Sum diff: " << std::abs(sum1 - sum2) << "\n";

    if (std::abs(sum1 - sum2) > 1e-9) {
        return 1;
    }
    return 0;
}
