#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>

#include <mutex>
#include <array>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <algorithm>

#include "TestIndexer.h"
#include "ProjectConfig.h"
#include "CacheDatabase.h"
#include "BlockchainReader.h"
#include "ConfigFileReader.h"
#include "SimpleMiddleware.h"
#include "BlockchainIndexer.h"

std::atomic<bool> reachedEnd;
BlockchainIndexer::ProjectConfig config;
std::shared_ptr<BlockchainIndexer::CacheDatabase> cacheDatabase;
std::shared_ptr<BlockchainIndexer::BlockListener> indexSubscriber;
std::shared_ptr<BlockchainIndexer::BlockchainReader> blockchainReader;
std::shared_ptr<BlockchainIndexer::BlockchainIndexer> blockchainIndexer;

// callback function to be used to get block from block hash
bool getBlock(std::string hash, BlockchainIndexer::Block& block)
{
    return cacheDatabase->getBlock(hash, block);
}

// callback function to be used to get block from that is currently the max height stored in the index
bool getMaxHeightBlock(BlockchainIndexer::Block& block)
{
    return cacheDatabase->getBlockWithMaxHeight(block);
}

// callback function to be used to get block from block height
bool getBlockWithHeight(int height, BlockchainIndexer::Block& block)
{
    return cacheDatabase->getBlockWithHeight(height, block);
}

bool getAllBlocks(std::vector<BlockchainIndexer::Block>& blocks)
{
    return cacheDatabase->getAllBlocks(blocks);
}

// callback function to be used to get transactions in block from block height
bool getTransactionsWithHeight(int height, std::vector<BlockchainIndexer::Transaction>& transactions)
{
    return cacheDatabase->getBlockTransactions(height, transactions);
}

// callback function to be used to get transactions in block from block hash
bool getTransactionsWithHash(std::string hash, std::vector<BlockchainIndexer::Transaction>& transactions)
{
    return cacheDatabase->getBlockTransactions(hash, transactions);
}

bool getTransactionWithId(std::string transId, BlockchainIndexer::Transaction& transaction)
{
    return cacheDatabase->getTransactionsWithId(transId, transaction);
}

// callback function to be used to get all updated transactions for an address
bool getAddressTransactions(std::string address, std::vector<BlockchainIndexer::TransactionOutput>& transactions)
{
    return cacheDatabase->getAddressTransactions(address, transactions);
}

// callback function to be used to get all spent transactions for an address
bool getAddressInputTransactions(std::string address, std::vector<BlockchainIndexer::TransactionInput>& transactions)
{
    return cacheDatabase->getAddressInputTransactions(address, transactions);
}

// callback function to be used to get all output transactions for an address
bool getAddressOutputTransactions(std::string address, std::vector<BlockchainIndexer::TransactionOutput>& transactions)
{
    return cacheDatabase->getAddressOutputTransactions(address, transactions);
}

void runReader()
{
    std::cout << "Starting blockchain reader." << std::endl;
    blockchainReader->logic();
    reachedEnd.store(true);
    std::cout << "Reached end of blockchain. Closing reader thread." << std::endl;
}

void runIndexer()
{
    std::cout << "Starting indexing logic thread." << std::endl;
    
    while (true)
    {
        if (indexSubscriber->haveNewMessage())
        {
            BlockchainIndexer::Block tmp;
            indexSubscriber->getMessage(tmp);

            if (tmp.confirmations >= config.xConfirmations)
            {
                // cache block data in memory
                cacheDatabase->cacheBlock(tmp);

                // index block data on disk
                blockchainIndexer->indexBlock(tmp);
                std::cout << "Processed block information." << std::endl;
            }
            else
            {
                std::cout << "Block confirmation below x-confirmation number, discarding." << std::endl;
            }
        }
        else
        {
            if (reachedEnd.load())
            {
                break;
            }
        }
    }

    std::cout << "Finished loading blockchain indexer, closing indexer logic thread." << std::endl;
}

int main(const int argc, const char** const argv)
{
    std::string configFilePath = argv[1];

    // get config
    BlockchainIndexer::ConfigFileReader configReader;
    configReader.init(configFilePath);
    configReader.getConfiguration(config);
    reachedEnd.store(false);

    // initialize middleware and subscriber
    indexSubscriber = std::make_shared<BlockchainIndexer::BlockListener>();
    std::shared_ptr<BlockchainIndexer::Middleware> middleware = std::make_shared<BlockchainIndexer::Middleware>();
    middleware->addSubscriber(indexSubscriber);

    // initialize reader
    blockchainReader = std::make_shared<BlockchainIndexer::BlockchainReader>();
    blockchainReader->init(config.blockchainFile, config.maxBlocks, config.publishPeriod);
    blockchainReader->addMiddleware(middleware);

    // initialize indexer
    blockchainIndexer = std::make_shared<BlockchainIndexer::BlockchainIndexer>();
    blockchainIndexer->init(config.databaseDirectory);

    // initialize cache database in heap
    cacheDatabase.reset(new BlockchainIndexer::CacheDatabase());

    // start indexing worker thread and reader thread
    std::thread readerThread(runReader);
    std::thread indexerThread(runIndexer);
    readerThread.join();
    indexerThread.join();

    if (config.runTest)
    {
        // run test cases
        std::cout << "Running test cases." << std::endl;
        BlockchainIndexer::TestIndexer tester(config.numBlockTestCases, config.numAddressTestCases, config.blockTestDirectory, config.addressTestDirectory);
        tester.init(cacheDatabase, blockchainIndexer);
        if (tester.runBlockTests())
        {
            std::cout << "Passed block test cases." << std::endl;
        }
        else
        {
            std::cout << "Failed block test cases." << std::endl;
        }

        if (tester.runAddressTests())
        {
            std::cout << "Passed address test cases." << std::endl;
        }
        else
        {
            std::cout << "Failed address test cases." << std::endl;
        }
    }

	return 0;
}