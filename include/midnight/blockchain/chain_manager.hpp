#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>

namespace midnight::blockchain
{

    class Block;
    class Transaction;

    /**
     * @brief Blockchain chain manager
     */
    class ChainManager
    {
    public:
        /**
         * @brief Initialize blockchain
         */
        void initialize();

        /**
         * @brief Add block to chain
         */
        bool add_block(std::shared_ptr<Block> block);

        /**
         * @brief Get block by height
         */
        std::shared_ptr<Block> get_block(uint64_t height) const;

        /**
         * @brief Get block by hash
         */
        std::shared_ptr<Block> get_block_by_hash(const std::string &hash) const;

        /**
         * @brief Get chain height
         */
        uint64_t get_height() const { return chain_height_.load(std::memory_order_acquire); }

        /**
         * @brief Validate block
         */
        bool validate_block(const Block &block) const;

        /**
         * @brief Get latest block
         */
        std::shared_ptr<Block> get_latest_block() const;

    private:
        std::vector<std::shared_ptr<Block>> blockchain_;
        std::atomic<uint64_t> chain_height_{0};
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, size_t> hash_to_index_;
    };

} // namespace midnight::blockchain
