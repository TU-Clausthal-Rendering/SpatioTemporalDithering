#pragma once
#include "Falcor.h"
#include <fstream>

template<int T>
inline int torusDistance(int x1, int y1, int x2, int y2) {
    int dx = std::min<int>(std::abs(x1 - x2), T - std::abs(x1 - x2));
    int dy = std::min<int>(std::abs(y1 - y2), T - std::abs(y1 - y2));
    return dx + dy;
}

template<int T>
inline double torusDistanceL2(int x1, int y1, int x2, int y2) {
    int dx = std::min<int>(std::abs(x1 - x2), T - std::abs(x1 - x2));
    int dy = std::min<int>(std::abs(y1 - y2), T - std::abs(y1 - y2));
    return std::sqrt(dx * dx + dy * dy);
}

template<int T>
inline double scorePermutation(const std::array<int, T* T>& indices) {
    // 2D Distance Score
    double sumInverseDist = 0.0;
    /*for (size_t i = 0; i < T * T; ++i) {
        for (size_t j = i + 1; j < T * T; ++j) {
            int x1 = i % T, y1 = i / T;
            int x2 = j % T, y2 = j / T;
            int valueDiff = std::abs(indices[i] - indices[j]);
            valueDiff *= valueDiff;
            int torusDist = torusDistance<T>(x1, y1, x2, y2);
            //double torusDist = torusDistanceL2<T>(x1, y1, x2, y2);

            if (torusDist == 1) {
                if (valueDiff == 1) return 0.0;
                sumInverseDist += (double)valueDiff / torusDist;
            }
        }
    }*/

    for (size_t i = 0; i < T; ++i) {
        for (size_t j = 0; j < T; ++j) {
            auto index = i + T * j;
            auto indexRight = (i + 1) % T + T * j;
            auto indexBot = i + T * ((j + 1) % T);
            auto diff1 = indices[index] - indices[indexRight];
            diff1 *= diff1;
            auto diff2 = indices[index] - indices[indexBot];
            diff2 *= diff2;

            if(diff1 == 1 || diff2 == 1) return 0.0;
            sumInverseDist += diff1 + diff2;
        }
    }

    return sumInverseDist;
}

template<int T>
inline std::vector<std::array<int, T* T>> generateBestPermutations(size_t maxResults = size_t(-1)) {
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

    /*
    double lastScore = 0;
    int count = 0;
    auto filename = "permutations.txt";
    std::ofstream file(filename);
    if (file.is_open()) {
        for (const auto& perm : scoredPermutations) {
            if (abs(perm.first - lastScore) > 0.01)
            {
                if (count > 0) file << "Total: " << count << "\n\n";

                lastScore = perm.first;
                count = 0;
                file << "Score: " << lastScore << "\n";
            }
            ++count;

            for (size_t i = 0; i < T; i++) {
                for (size_t j = 0; j < T; j++) {
                    file << perm.second[i * T + j] << " ";
                }
                file << "\n";
            }
            file << "-----\n";
        }
    }

    lastScore = 0;
    int printCount = 0;
    count = 0;
    // print a preview of the matrices
    for (const auto& perm : scoredPermutations) {
        if(abs(perm.first - lastScore) > 0.01)
        {
            if(count > 0) std::cout << "Total: " << count << "\n\n";

            printCount = 3;
            lastScore = perm.first;
            count = 0;
            std::cout << "Score: " << lastScore << "\n";
        }
        ++count;

        if(printCount <= 0) continue;
        --printCount;
        for (size_t i = 0; i < T; i++) {
            for (size_t j = 0; j < T; j++) {
                std::cout << perm.second[i * T + j] << " ";
            }
            std::cout << "\n";
        }
        std::cout << "-----\n";
    }
    std::cout << "Total: " << count << "\n\n";
    */
    // Return only the best results
    std::vector<std::array<int, T* T>> bestPermutations;
    for (size_t i = 0; i < std::min(maxResults, scoredPermutations.size()); ++i) {
        if(scoredPermutations[i].first == 0.0) break; // stop when score falls to zero
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

template<int T>
std::vector<std::array<int, T* T>> removeOverlapping(std::vector<std::array<int, T* T>> perms, size_t maxOverlap = 1)
{
    std::vector<std::array<int, T* T>> res;

    // Iterate through each candidate permutation.
    for (const auto& candidate : perms) {
        bool valid = true;

        // Compare candidate with every already accepted permutation.
        for (const auto& existing : res) {
            size_t overlapCount = 0;
            // Count the number of positions where both arrays are identical.
            for (size_t i = 0; i < candidate.size(); ++i) {
                if (candidate[i] == existing[i]) {
                    ++overlapCount;
                }
            }
            if (overlapCount > maxOverlap) {
                valid = false;
                break;  // Candidate overlaps too much with an existing permutation.
            }
        }

        // Add candidate if it meets the criteria with all accepted arrays.
        if (valid) {
            res.push_back(candidate);
        }
    }

    return res;
}


inline ref<Buffer> generatePermutations3x3(ref<Device> pDevice)
{
    // generate permutations, ranked from best to worst, exlcuding permutations with succesive values next to each other
    auto perms = generateBestPermutations<3>(288); // 
    std::cout << "Permutations without successive elements: " << perms.size() << "\n";
    //perms = removeOverlapping<3>(perms, 3);
    //std::cout << "Permutations after removing overlap: " << perms.size() << "\n";

    std::vector<uint32_t> packed(perms.size());
    std::transform(perms.begin(), perms.end(), packed.begin(), packPermutation);

    return Buffer::createStructured(pDevice, sizeof(packed[0]), packed.size(), ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, packed.data(), false);
}
