#pragma once

#include <vector>
#include <array>

// Opaque interface — CUDA types hidden from CXX compilation
class GpuMiner {
public:
    GpuMiner(int deviceId, int numThreads);
    ~GpuMiner();

    void updatePool(const unsigned char* miningSeed);
    void updatePublicKey(const unsigned char* pubKey);
    int mineBatch(std::vector<std::array<unsigned char, 32>>& foundNonces);
    int getNumThreads() const;

private:
    struct Impl;
    Impl* impl_;
};
