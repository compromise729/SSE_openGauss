#ifndef OMAP_H
#define OMAP_H
#include <iostream>
#include "ORAM.hpp"
#include "RAMStore.hpp"
#include <functional>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "AVLTree.h"
using namespace std;

class OMAP {
private:
    Bid rootKey;
    int rootPos;
    AVLTree* treeHandler;

public:
    OMAP(int maxSize, bytes<Key> key);
    virtual ~OMAP();
    void insert(Bid key, string value);
    string find(Bid key);
    void printTree();
    void batchInsert(map<Bid, string> pairs);
    vector<string> batchSearch(vector<Bid> keys);

    void serialize(std::ostream& os) const {
        std::cout << "  [OMAP::serialize] Begin at pos: " << os.tellp() << std::endl;

        rootKey.serialize(os);
        std::cout << "  [OMAP::serialize] rootKey serialized, pos: " << os.tellp() << std::endl;

        os.write(reinterpret_cast<const char*>(&rootPos), sizeof(rootPos));
        std::cout << "  [OMAP::serialize] rootPos = " << rootPos << ", pos: " << os.tellp() << std::endl;

        if (treeHandler) {
            std::cout << "  [OMAP::serialize] Serializing AVLTree..." << std::endl;
            treeHandler->serialize(os);
        }

        std::cout << "  [OMAP::serialize] Done at pos: " << os.tellp() << std::endl;
    }
    void deserialize(std::istream& is, int maxSize, bytes<Key> key) {
        std::cout << "  [OMAP::deserialize] Begin at pos: " << is.tellg() << std::endl;

        rootKey.deserialize(is);
        std::cout << "  [OMAP::deserialize] rootKey deserialized, pos: " << is.tellg() << std::endl;

        is.read(reinterpret_cast<char*>(&rootPos), sizeof(rootPos));
        std::cout << "  [OMAP::deserialize] rootPos = " << rootPos << ", pos: " << is.tellg() << std::endl;

        if (treeHandler) {
            delete treeHandler;
            treeHandler = nullptr;
        }

        treeHandler = new AVLTree(maxSize, key);
        std::cout << "  [OMAP::deserialize] Deserializing AVLTree..." << std::endl;
        treeHandler->deserialize(is, maxSize, key);

        std::cout << "  [OMAP::deserialize] Done at pos: " << is.tellg() << std::endl;
    }
};

#endif /* OMAP_H */

