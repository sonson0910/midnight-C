#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace midnight::crypto {

/**
 * @brief Merkle Tree for Midnight shielded state verification
 * 
 * Uses Blake2b-256 for hashing (Midnight standard)
 */
class MerkleTree {
public:
    static constexpr size_t DEPTH = 32;
    static constexpr size_t HASH_SIZE = 32;

    static MerkleTree from_leaves(const std::vector<std::vector<uint8_t>>& leaves);
    MerkleTree();
    
    const std::vector<uint8_t>& root() const { return root_; }
    std::optional<std::vector<uint8_t>> get_node(size_t level, size_t index) const;
    std::optional<std::vector<uint8_t>> get_leaf(size_t index) const;
    size_t leaf_count() const { return leaves_.size(); }
    bool empty() const { return leaves_.empty(); }
    
    std::optional<std::vector<std::vector<uint8_t>>> get_path(size_t leaf_index) const;

private:
    MerkleTree(std::vector<std::vector<uint8_t>> leaves, 
               std::vector<std::vector<std::vector<uint8_t>>> nodes,
               std::vector<uint8_t> root);
    
    std::vector<std::vector<uint8_t>> leaves_;
    std::vector<std::vector<std::vector<uint8_t>>> nodes_;
    std::vector<uint8_t> root_;
    
    void compute_tree();

public:  // Made public so MerkleProof can use these
    static std::vector<uint8_t> hash_pair(
        const std::vector<uint8_t>& left,
        const std::vector<uint8_t>& right);
    static std::vector<uint8_t> hash_single(
        const std::vector<uint8_t>& data);

private:
};

class MerkleProof {
public:
    MerkleProof(
        std::vector<uint8_t> leaf,
        std::vector<std::vector<uint8_t>> siblings,
        std::vector<size_t> sibling_positions,
        size_t leaf_index);

    bool verify(const std::vector<uint8_t>& expected_root) const;
    const std::vector<uint8_t>& leaf() const { return leaf_; }
    const std::vector<std::vector<uint8_t>>& siblings() const { return siblings_; }
    size_t leaf_index() const { return leaf_index_; }

    std::string leaf_hex() const;
    std::vector<std::string> siblings_hex() const;
    std::string to_json() const;
    static std::optional<MerkleProof> from_json(const std::string& json_str);

private:
    std::vector<uint8_t> leaf_;
    std::vector<std::vector<uint8_t>> siblings_;
    std::vector<size_t> sibling_positions_;
    size_t leaf_index_;
    
    std::vector<uint8_t> compute_root() const;
};

std::vector<uint8_t> blake2b_256(const std::vector<uint8_t>& data);
std::vector<uint8_t> blake2b_256(const uint8_t* data, size_t len);

std::string to_hex(const std::vector<uint8_t>& data);
std::optional<std::vector<uint8_t>> from_hex(const std::string& hex);

} // namespace midnight::crypto
