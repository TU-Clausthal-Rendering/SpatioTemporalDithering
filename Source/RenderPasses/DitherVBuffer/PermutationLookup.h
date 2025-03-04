#pragma once
#include "Falcor.h"

template<int T>
inline int torusDistance(int x1, int y1, int x2, int y2) {
    int dx = std::min<int>(std::abs(x1 - x2), T - std::abs(x1 - x2));
    int dy = std::min<int>(std::abs(y1 - y2), T - std::abs(y1 - y2));
    return dx + dy;
}

template<int T>
inline double scorePermutation(const std::array<int, T* T>& indices) {
    // 2D Distance Score
    double sumInverseDist = 0.0;
    for (size_t i = 0; i < T * T; ++i) {
        for (size_t j = i + 1; j < T * T; ++j) {
            int x1 = i % T, y1 = i / T;
            int x2 = j % T, y2 = j / T;
            int valueDiff = std::abs(indices[i] - indices[j]);
            int torusDist = torusDistance<T>(x1, y1, x2, y2);

            if (torusDist > 0) {
                sumInverseDist += (double)valueDiff / torusDist;
            }
        }
    }

    return sumInverseDist;
    // 1D Offset Score (with Wrap-Around)
    /*int min1DSpacing = std::numeric_limits<int>::max();
    int sum1DSpacing = 0;
    for (size_t i = 0; i < T * T; ++i) {
        size_t nextIndex = (i + 1) % (T * T);  // Wrap-around at the end
        int spacing = std::abs(indices[i] - indices[nextIndex]);
        min1DSpacing = std::min(min1DSpacing, spacing);
        sum1DSpacing += spacing;
    }

    // Combine Scores (adjust weights as needed)
    return sumInverseDist + min1DSpacing * T * T + sum1DSpacing * 3.0;*/
}

template<int T>
inline std::vector<std::array<int, T* T>> generateBestPermutations(size_t maxResults = 10) {
    std::array<int, T * T> indices;
    std::iota(indices.begin(), indices.end(), 0);

    std::vector<std::pair<double, std::array<int, T* T>>> scoredPermutations;

    do {
        double score = scorePermutation<T>(indices);
        scoredPermutations.push_back({ score, indices });
    } while (std::next_permutation(indices.begin(), indices.end()));

    // Sort permutations by best score (higher is better)
    std::sort(scoredPermutations.begin(), scoredPermutations.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    // Return only the best results
    std::vector<std::array<int, T* T>> bestPermutations;
    for (size_t i = 0; i < std::min(maxResults, scoredPermutations.size()); ++i) {
        bestPermutations.push_back(scoredPermutations[i].second);
    }

    return bestPermutations;
}

// Function to pack 8 values (0-8) into a single uint32_t using 4 bits per value
inline uint32_t packPermutation(const std::array<int, 9>& indices) {
    uint32_t packed = 0;
    for (size_t i = 0; i < 8; ++i) {
        packed |= (indices[i] & 0xF) << (i * 4);
    }
    return packed;
}

inline ref<Buffer> generatePermutations3x3(ref<Device> pDevice)
{
    auto perms = generateBestPermutations<3>(256);

    std::vector<uint32_t> packed(perms.size());
    std::transform(perms.begin(), perms.end(), packed.begin(), packPermutation);

    return Buffer::createStructured(pDevice, sizeof(packed[0]), packed.size(), ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, packed.data(), false);
}

template<int T>
inline void generatePermutations()
{
    auto bestPermutations = generateBestPermutations<T>(10);

    std::cout << "Top " << bestPermutations.size() << " Permutations:\n";
    for (const auto& perm : bestPermutations) {
        for (size_t i = 0; i < T; i++) {
            for (size_t j = 0; j < T; j++) {
                std::cout << perm[i * T + j] << " ";
            }
            std::cout << "\n";
        }
        std::cout << "-----\n";
    }
}
