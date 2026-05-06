#include "midnight/blockchain/chain_manager.hpp"
#include "midnight/blockchain/block.hpp"
#include "midnight/core/logger.hpp"
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <chrono>

namespace midnight::blockchain
{

    void ChainManager::initialize()
    {
        std::unique_lock lock(mutex_);
        if (!blockchain_.empty()) return;

        auto genesis = std::make_shared<Block>(0, "0");
        blockchain_.push_back(genesis);
        chain_height_.store(1, std::memory_order_release);
        hash_to_index_[genesis->calculate_hash()] = 0;

        if (midnight::g_logger) {
            midnight::g_logger->info("Blockchain initialized, genesis: " + genesis->calculate_hash());
        }
    }

    bool ChainManager::add_block(std::shared_ptr<Block> block)
    {
        if (!block) {
            if (midnight::g_logger)
                midnight::g_logger->error("add_block: null block");
            return false;
        }

        {
            std::unique_lock lock(mutex_);

            if (!validate_block(*block)) {
                if (midnight::g_logger)
                    midnight::g_logger->error("Block validation failed: " + block->calculate_hash());
                return false;
            }

            blockchain_.push_back(block);
            chain_height_.store(blockchain_.size(), std::memory_order_release);
            hash_to_index_[block->calculate_hash()] = blockchain_.size() - 1;
        }

        if (midnight::g_logger) {
            midnight::g_logger->info("Block added, height=" + std::to_string(block->get_header().height));
        }
        return true;
    }

    std::shared_ptr<Block> ChainManager::get_block(uint64_t height) const
    {
        std::shared_lock lock(mutex_);
        if (height >= blockchain_.size()) return nullptr;
        return blockchain_[height];
    }

    std::shared_ptr<Block> ChainManager::get_block_by_hash(const std::string &hash) const
    {
        std::shared_lock lock(mutex_);
        auto it = hash_to_index_.find(hash);
        if (it == hash_to_index_.end()) return nullptr;
        return blockchain_[it->second];
    }

    bool ChainManager::validate_block(const Block &block) const
    {
        std::shared_lock lock(mutex_);
        if (blockchain_.empty()) {
            return block.get_header().height == 0;
        }

        const auto &prev = blockchain_.back();
        const auto &hdr  = block.get_header();

        // Height must be exactly one ahead
        if (hdr.height != prev->get_header().height + 1) {
            if (midnight::g_logger) {
                midnight::g_logger->warn("Block height gap: expected="
                    + std::to_string(prev->get_header().height + 1)
                    + " got=" + std::to_string(hdr.height));
            }
            return false;
        }

        // prev_hash must match the previous block's hash
        if (hdr.prev_hash != prev->calculate_hash()) {
            if (midnight::g_logger)
                midnight::g_logger->warn("Block prev_hash mismatch");
            return false;
        }

        // Block timestamp must not be excessively in the future (allow 60s clock drift)
        auto now_sec = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        if (hdr.timestamp > now_sec + 60) {
            if (midnight::g_logger)
                midnight::g_logger->warn("Block timestamp too far in the future");
            return false;
        }

        // Block timestamp must be after previous block's timestamp
        if (hdr.timestamp <= prev->get_header().timestamp) {
            if (midnight::g_logger)
                midnight::g_logger->warn("Block timestamp not after previous block");
            return false;
        }

        return true;
    }

    std::shared_ptr<Block> ChainManager::get_latest_block() const
    {
        std::shared_lock lock(mutex_);
        if (blockchain_.empty()) return nullptr;
        return blockchain_.back();
    }

} // namespace midnight::blockchain
