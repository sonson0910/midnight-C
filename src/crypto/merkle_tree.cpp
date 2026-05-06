#include "midnight/crypto/merkle_tree.hpp"
#include "midnight/core/logger.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#if defined(MIDNIGHT_ENABLE_SODIUM) && MIDNIGHT_ENABLE_SODIUM && __has_include(<sodium.h>)
#include <sodium.h>
#define MIDNIGHT_HAS_SODIUM 1
#else
#define MIDNIGHT_HAS_SODIUM 0
#endif

namespace midnight::crypto {

// ─── Blake2b-256 Hash ──────────────────────────────────────────

std::vector<uint8_t> blake2b_256(const std::vector<uint8_t>& data) {
    return blake2b_256(data.data(), data.size());
}

std::vector<uint8_t> blake2b_256(const uint8_t* data, size_t len) {
    std::vector<uint8_t> hash(MerkleTree::HASH_SIZE);

#if MIDNIGHT_HAS_SODIUM
    crypto_generichash(hash.data(), MerkleTree::HASH_SIZE, data, len, nullptr, 0);
#else
    throw std::runtime_error("libsodium required for Blake2b-256 hashing");
#endif
    
    return hash;
}

// ─── Hex Utilities ─────────────────────────────────────────────

std::string to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

std::optional<std::vector<uint8_t>> from_hex(const std::string& hex) {
    std::vector<uint8_t> result;
    std::string clean_hex = hex;
    
    if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) {
        clean_hex = hex.substr(2);
    }
    
    if (clean_hex.length() % 2 != 0) {
        return std::nullopt;
    }
    
    for (size_t i = 0; i < clean_hex.length(); i += 2) {
        try {
            unsigned long byte_val = std::stoul(clean_hex.substr(i, 2), nullptr, 16);
            result.push_back(static_cast<uint8_t>(byte_val));
        } catch (...) {
            return std::nullopt;
        }
    }
    
    return result;
}

// ─── MerkleTree ────────────────────────────────────────────────

MerkleTree::MerkleTree() : root_(HASH_SIZE, 0) {}

MerkleTree MerkleTree::from_leaves(const std::vector<std::vector<uint8_t>>& leaves) {
    std::vector<std::vector<uint8_t>> normalized_leaves;
    normalized_leaves.reserve(leaves.size());
    
    for (const auto& leaf : leaves) {
        std::vector<uint8_t> normalized_leaf(HASH_SIZE, 0);
        size_t copy_len = std::min(leaf.size(), HASH_SIZE);
        std::copy(leaf.begin(), leaf.begin() + copy_len, normalized_leaf.begin());
        normalized_leaves.push_back(std::move(normalized_leaf));
    }
    
    std::vector<std::vector<std::vector<uint8_t>>> nodes;
    nodes.resize(DEPTH);
    
    std::vector<std::vector<uint8_t>> current_level = normalized_leaves;
    
    for (size_t level = 0; level < DEPTH; ++level) {
        nodes[level] = current_level;
        
        if (current_level.size() <= 1) {
            break;
        }
        
        std::vector<std::vector<uint8_t>> next_level;
        next_level.reserve((current_level.size() + 1) / 2);
        
        size_t i = 0;
        while (i < current_level.size()) {
            if (i + 1 < current_level.size()) {
                next_level.push_back(hash_pair(current_level[i], current_level[i + 1]));
            } else {
                next_level.push_back(hash_pair(current_level[i], std::vector<uint8_t>(HASH_SIZE, 0)));
            }
            i += 2;
        }
        
        current_level = std::move(next_level);
    }
    
    std::vector<uint8_t> root;
    if (!current_level.empty()) {
        root = current_level[0];
    } else {
        root = std::vector<uint8_t>(HASH_SIZE, 0);
    }
    
    return MerkleTree(std::move(normalized_leaves), std::move(nodes), std::move(root));
}

MerkleTree::MerkleTree(
    std::vector<std::vector<uint8_t>> leaves,
    std::vector<std::vector<std::vector<uint8_t>>> nodes,
    std::vector<uint8_t> root)
    : leaves_(std::move(leaves))
    , nodes_(std::move(nodes))
    , root_(std::move(root)) {}

void MerkleTree::compute_tree() {}

std::optional<std::vector<uint8_t>> MerkleTree::get_node(size_t level, size_t index) const {
    if (level >= nodes_.size()) {
        return std::nullopt;
    }
    if (index >= nodes_[level].size()) {
        return std::nullopt;
    }
    return nodes_[level][index];
}

std::optional<std::vector<uint8_t>> MerkleTree::get_leaf(size_t index) const {
    if (index >= leaves_.size()) {
        return std::nullopt;
    }
    return leaves_[index];
}

std::optional<std::vector<std::vector<uint8_t>>> MerkleTree::get_path(size_t leaf_index) const {
    if (leaf_index >= leaves_.size()) {
        return std::nullopt;
    }
    
    std::vector<std::vector<uint8_t>> path;
    size_t idx = leaf_index;
    
    for (size_t level = 0; level < nodes_.size() && level < DEPTH; ++level) {
        if (nodes_[level].empty()) {
            break;
        }
        
        size_t sibling_idx;
        if (idx % 2 == 0) {
            sibling_idx = idx + 1;
        } else {
            sibling_idx = idx - 1;
        }
        
        if (sibling_idx < nodes_[level].size()) {
            path.push_back(nodes_[level][sibling_idx]);
        } else {
            path.push_back(std::vector<uint8_t>(HASH_SIZE, 0));
        }
        
        idx = idx / 2;
    }
    
    return path;
}

std::vector<uint8_t> MerkleTree::hash_pair(
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right) {
    std::vector<uint8_t> combined;
    combined.reserve(left.size() + right.size());
    combined.insert(combined.end(), left.begin(), left.end());
    combined.insert(combined.end(), right.begin(), right.end());
    return blake2b_256(combined);
}

std::vector<uint8_t> MerkleTree::hash_single(const std::vector<uint8_t>& data) {
    return blake2b_256(data);
}

// ─── MerkleProof ───────────────────────────────────────────────

MerkleProof::MerkleProof(
    std::vector<uint8_t> leaf,
    std::vector<std::vector<uint8_t>> siblings,
    std::vector<size_t> sibling_positions,
    size_t leaf_index)
    : leaf_(std::move(leaf))
    , siblings_(std::move(siblings))
    , sibling_positions_(std::move(sibling_positions))
    , leaf_index_(leaf_index) {}

std::vector<uint8_t> MerkleProof::compute_root() const {
    std::vector<uint8_t> current = leaf_;
    
    for (size_t i = 0; i < siblings_.size(); ++i) {
        if (i < sibling_positions_.size()) {
            if (sibling_positions_[i] == 0) {
                current = MerkleTree::hash_pair(siblings_[i], current);
            } else {
                current = MerkleTree::hash_pair(current, siblings_[i]);
            }
        } else {
            current = MerkleTree::hash_pair(current, std::vector<uint8_t>(MerkleTree::HASH_SIZE, 0));
        }
    }
    
    return current;
}

bool MerkleProof::verify(const std::vector<uint8_t>& expected_root) const {
    if (leaf_.empty() || expected_root.empty()) {
        return false;
    }
    
    std::vector<uint8_t> computed = compute_root();
    
    if (computed.size() != expected_root.size()) {
        return false;
    }
    
    return std::equal(computed.begin(), computed.end(), expected_root.begin());
}

std::string MerkleProof::leaf_hex() const {
    return to_hex(leaf_);
}

std::vector<std::string> MerkleProof::siblings_hex() const {
    std::vector<std::string> result;
    result.reserve(siblings_.size());
    for (const auto& s : siblings_) {
        result.push_back(to_hex(s));
    }
    return result;
}

std::string MerkleProof::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"leaf\":\"" << leaf_hex() << "\",";
    oss << "\"leaf_index\":" << leaf_index_ << ",";
    oss << "\"siblings\":[";
    for (size_t i = 0; i < siblings_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << to_hex(siblings_[i]) << "\"";
    }
    oss << "],";
    oss << "\"sibling_positions\":[";
    for (size_t i = 0; i < sibling_positions_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << sibling_positions_[i];
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

std::optional<MerkleProof> MerkleProof::from_json(const std::string& json_str) {
    try {
        std::string s = json_str;
        auto leaf_start = s.find("\"leaf\":\"");
        auto leaf_end = s.find("\"", leaf_start + 8);
        auto leaf_hex_val = s.substr(leaf_start + 8, leaf_end - leaf_start - 8);
        auto leaf = from_hex(leaf_hex_val);
        if (!leaf || leaf->size() != MerkleTree::HASH_SIZE) {
            return std::nullopt;
        }
        
        auto idx_start = s.find("\"leaf_index\":");
        auto idx_end = s.find(",", idx_start);
        size_t idx = std::stoull(s.substr(idx_start + 13, idx_end - idx_start - 13));
        
        std::vector<std::vector<uint8_t>> siblings;
        std::vector<size_t> positions;
        
        auto sib_start = s.find("\"siblings\":[");
        auto sib_end = s.find("]", sib_start);
        std::string sib_str = s.substr(sib_start + 12, sib_end - sib_start - 12);
        
        size_t pos = 0;
        while (pos < sib_str.length()) {
            auto q1 = sib_str.find("\"", pos);
            if (q1 == std::string::npos) break;
            auto q2 = sib_str.find("\"", q1 + 1);
            auto hex_val = sib_str.substr(q1 + 1, q2 - q1 - 1);
            auto sib = from_hex(hex_val);
            if (sib && sib->size() == MerkleTree::HASH_SIZE) {
                siblings.push_back(*sib);
            }
            pos = q2 + 1;
            if (pos < sib_str.length() && sib_str[pos] == ',') pos++;
        }
        
        auto pos_start = s.find("\"sibling_positions\":[");
        auto pos_end = s.find("]", pos_start);
        std::string pos_str = s.substr(pos_start + 21, pos_end - pos_start - 21);
        
        size_t p = 0;
        while (p < pos_str.length()) {
            while (p < pos_str.length() && (pos_str[p] == ',' || pos_str[p] == ' ')) p++;
            if (p >= pos_str.length()) break;
            size_t comma = pos_str.find(',', p);
            size_t end = (comma == std::string::npos) ? pos_str.length() : comma;
            positions.push_back(std::stoull(pos_str.substr(p, end - p)));
            p = end + 1;
        }
        
        return MerkleProof(*leaf, siblings, positions, idx);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace midnight::crypto
