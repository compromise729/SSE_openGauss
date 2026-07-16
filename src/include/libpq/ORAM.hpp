#ifndef ORAM_H
#define ORAM_H

#include "AES.hpp"
#include <random>
#include <vector>
#include <unordered_map>
#include <string>
#include <iostream>
#include "RAMStore.hpp"
#include <map>
#include <set>
#include <bits/stdc++.h>
#include "Bid.h"

using namespace std;

class Orion_Node {
public:

    Orion_Node() {
    }

    ~Orion_Node() {
    }
    Bid key;
    std::array< byte_t, 16> value;
    int pos;
    Bid leftID;
    int leftPos;
    Bid rightID;
    int rightPos;
    unsigned int height;

    
    // Orion_Node.h 中添加
    void serialize(std::ostream& os) const {
        key.serialize(os);

        for (const auto& b : value) {
            os.write(reinterpret_cast<const char*>(&b), sizeof(byte_t));
        }

        os.write(reinterpret_cast<const char*>(&pos), sizeof(pos));

        leftID.serialize(os);
        os.write(reinterpret_cast<const char*>(&leftPos), sizeof(leftPos));

        rightID.serialize(os);
        os.write(reinterpret_cast<const char*>(&rightPos), sizeof(rightPos));

        os.write(reinterpret_cast<const char*>(&height), sizeof(height));
    }

    void deserialize(std::istream& is) {
        key.deserialize(is);

        for (auto& b : value) {
            is.read(reinterpret_cast<char*>(&b), sizeof(byte_t));
        }

        is.read(reinterpret_cast<char*>(&pos), sizeof(pos));

        leftID.deserialize(is);
        is.read(reinterpret_cast<char*>(&leftPos), sizeof(leftPos));

        rightID.deserialize(is);
        is.read(reinterpret_cast<char*>(&rightPos), sizeof(rightPos));

        is.read(reinterpret_cast<char*>(&height), sizeof(height));
    }
};

struct Orion_Block {
    Bid id;
    block data;
};

using Bucket = std::array<Orion_Block, Z>;

class ORAM {
private:
    RAMStore* store;
    using Stash = std::unordered_map<Bid, block>;
    size_t depth;
    size_t blockSize;
    map<Bid, Orion_Node*> cache;
    vector<int> leafList;
    vector<int> readviewmap;
    vector<int> writeviewmap;
    set<Bid> modified;
    int readCnt = 0;
    bytes<Key> key;

    // Randomness
    std::random_device rd;
    std::mt19937 mt;
    std::uniform_int_distribution<int> dis;

    int RandomPath();
    int GetNodeOnPath(int leaf, int depth);
    std::vector<Bid> GetIntersectingBlocks(int x, int depth);

    void FetchPath(int leaf);
    void WritePath(int leaf, int level);

    Orion_Node* ReadData(Bid bid);
    void WriteData(Bid bid, Orion_Node* b);

    block SerialiseBucket(Bucket bucket);
    Bucket DeserialiseBucket(block buffer);

    Bucket ReadBucket(int pos);
    void WriteBucket(int pos, Bucket bucket);
    void Access(Bid bid, Orion_Node*& node, int lastLeaf, int newLeaf);
    void Access(Bid bid, Orion_Node*& node);


    size_t plaintext_size;
    size_t bucketCount;
    size_t clen_size;
    bool batchWrite = false;

    bool WasSerialised();
    void Print();

public:
    ORAM(int maxSize, bytes<Key> key);
    ~ORAM();

    Orion_Node* ReadNode(Bid bid, int lastLeaf, int newLeaf);
    Orion_Node* ReadNode(Bid bid);
    int WriteNode(Bid bid, Orion_Node* n);
    void start(bool batchWrite);
    void finilize(bool find, Bid& rootKey, int& rootPos);
    static Orion_Node* convertBlockToNode(block b);
    static block convertNodeToBlock(Orion_Node* node);

    void serialize(std::ostream& os) const {
        cout << "      [ORAM Serialize] Start" << endl;
        cout << "[Serialize] ORAM this = " << this << endl;

        // 保存 depth, blockSize, readCnt
        os.write(reinterpret_cast<const char*>(&depth), sizeof(depth));
        os.write(reinterpret_cast<const char*>(&blockSize), sizeof(blockSize));
        os.write(reinterpret_cast<const char*>(&readCnt), sizeof(readCnt));
        cout << "      [ORAM Serialize] depth=" << depth << ", blockSize=" << blockSize << ", readCnt=" << readCnt << endl;

        // 保存 key
        size_t keySize = key.size();
        os.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
        os.write(reinterpret_cast<const char*>(key.data()), keySize);
        cout << "      [ORAM Serialize] key size = " << keySize << endl;

        // 保存 leafList
        size_t leafSize = leafList.size();
        os.write(reinterpret_cast<const char*>(&leafSize), sizeof(leafSize));
        os.write(reinterpret_cast<const char*>(leafList.data()), leafSize * sizeof(int));
        cout << "      [ORAM Serialize] leafList size = " << leafSize << endl;

        // 保存 readviewmap
        size_t readMapSize = readviewmap.size();
        os.write(reinterpret_cast<const char*>(&readMapSize), sizeof(readMapSize));
        os.write(reinterpret_cast<const char*>(readviewmap.data()), readMapSize * sizeof(int));
        cout << "      [ORAM Serialize] readviewmap size = " << readMapSize << endl;

        // 保存 writeviewmap
        size_t writeMapSize = writeviewmap.size();
        os.write(reinterpret_cast<const char*>(&writeMapSize), sizeof(writeMapSize));
        os.write(reinterpret_cast<const char*>(writeviewmap.data()), writeMapSize * sizeof(int));
        cout << "      [ORAM Serialize] writeviewmap size = " << writeMapSize << endl;

        // 保存 modified 集合
        size_t modSize = modified.size();
        os.write(reinterpret_cast<const char*>(&modSize), sizeof(modSize));
        cout << "      [ORAM Serialize] modified size = " << modSize << endl;
        for (const auto& bid : modified) {
            bid.serialize(os);
        }

        // 保存 cache
        size_t cacheSize = cache.size();
        os.write(reinterpret_cast<const char*>(&cacheSize), sizeof(cacheSize));
        cout << "      [ORAM Serialize] cache size = " << cacheSize << endl;

        for (const auto& pair : cache) {
            pair.first.serialize(os);
            cout << "      [ORAM Serialize] cache Bid = ";
            for (auto b : pair.first.id) cout << int(b) << "-";
            cout << endl;

            if (pair.second) {
                pair.second->serialize(os);
            } else {
                cout << "      [ORAM Serialize] Warning: Orion_Node* is NULL for this Bid!" << endl;
            }
        }

        cout << "      [ORAM Serialize] Serializing RAMStore..." << endl;
        store->serialize(os);
        cout << "      [ORAM Serialize] RAMStore serialized." << endl;

        cout << "      [ORAM Serialize] End" << endl;
    }

    void deserialize(std::istream& is) {
        cout << "      [ORAM Deserialize] Start" << endl;

        // 读取 depth, blockSize, readCnt
        is.read(reinterpret_cast<char*>(&depth), sizeof(depth));
        is.read(reinterpret_cast<char*>(&blockSize), sizeof(blockSize));
        is.read(reinterpret_cast<char*>(&readCnt), sizeof(readCnt));
        cout << "      [ORAM Deserialize] depth=" << depth << ", blockSize=" << blockSize << ", readCnt=" << readCnt << endl;

        // 读取 key
        size_t keySize;
        is.read(reinterpret_cast<char*>(&keySize), sizeof(keySize));
        if (keySize != Key) {
            throw std::runtime_error("Key size mismatch during deserialization.");
        }
        is.read(reinterpret_cast<char*>(key.data()), keySize);
        cout << "      [ORAM Deserialize] key size = " << keySize << endl;

        // 读取 leafList
        size_t leafSize;
        is.read(reinterpret_cast<char*>(&leafSize), sizeof(leafSize));
        leafList.resize(leafSize);
        is.read(reinterpret_cast<char*>(leafList.data()), leafSize * sizeof(int));
        cout << "      [ORAM Deserialize] leafList size = " << leafSize << endl;

        // 读取 readviewmap
        size_t readMapSize;
        is.read(reinterpret_cast<char*>(&readMapSize), sizeof(readMapSize));
        readviewmap.resize(readMapSize);
        is.read(reinterpret_cast<char*>(readviewmap.data()), readMapSize * sizeof(int));
        cout << "      [ORAM Deserialize] readviewmap size = " << readMapSize << endl;

        // 读取 writeviewmap
        size_t writeMapSize;
        is.read(reinterpret_cast<char*>(&writeMapSize), sizeof(writeMapSize));
        writeviewmap.resize(writeMapSize);
        is.read(reinterpret_cast<char*>(writeviewmap.data()), writeMapSize * sizeof(int));
        cout << "      [ORAM Deserialize] writeviewmap size = " << writeMapSize << endl;

        // 读取 modified 集合
        size_t modSize;
        is.read(reinterpret_cast<char*>(&modSize), sizeof(modSize));
        modified.clear();
        for (size_t i = 0; i < modSize; ++i) {
            Bid bid;
            bid.deserialize(is);
            modified.insert(bid);
        }
        cout << "      [ORAM Deserialize] modified size = " << modSize << endl;

        // 读取 cache
        size_t cacheSize;
        is.read(reinterpret_cast<char*>(&cacheSize), sizeof(cacheSize));
        cache.clear();
        cout << "      [ORAM Deserialize] cache size = " << cacheSize << endl;

        for (size_t i = 0; i < cacheSize; ++i) {
            Bid bid;
            bid.deserialize(is);
            cout << "      [ORAM Deserialize] cache Bid = ";
            for (auto b : bid.id) cout << int(b) << "-";
            cout << endl;

            Orion_Node* node = new Orion_Node();
            node->deserialize(is);
            cache[bid] = node;
        }

        cout << "      [ORAM Deserialize] Deserializing RAMStore..." << endl;
        store->deserialize(is);
        cout << "      [ORAM Deserialize] RAMStore deserialized." << endl;


        cout << "      [ORAM Deserialize] End" << endl;
    }
};

#endif
