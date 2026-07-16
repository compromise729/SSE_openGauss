#ifndef AVLTREE_H
#define AVLTREE_H
#include <iostream>
#include "ORAM.hpp"
#include "RAMStore.hpp"
#include <functional>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <array>
#include <memory>
#include <type_traits>
#include <iomanip>
#include <bits/stdc++.h>
#include "Bid.h"
#include <random>
using namespace std;

class AVLTree {
private:
    ORAM *oram;
    std::random_device rd;
    std::mt19937 mt;
    std::uniform_int_distribution<int> dis;

    int height(Bid N, int& leaf);
    int max(int a, int b);
    Orion_Node* newNode(Bid key, string value);
    Orion_Node* rightRotate(Orion_Node* y);
    Orion_Node* leftRotate(Orion_Node* x);
    int getBalance(Orion_Node* N);
    int RandomPath();

public:
    AVLTree(int maxSize, bytes<Key> key);
    virtual ~AVLTree();
    Bid insert(Bid rootKey, int& pos, Bid key, string value);
    Orion_Node* search(Orion_Node* head, Bid key);
    void batchSearch(Orion_Node* head, vector<Bid> keys, vector<Orion_Node*>* results);
    void printTree(Orion_Node* root, int indent);
    void startOperation(bool batchWrite = false);
    void finishOperation(bool find, Bid& rootKey, int& rootPos);

    void serialize(std::ostream& os) const {
        std::cout << "    [AVLTree::serialize] Serializing ORAM, pos: " << os.tellp() << std::endl;
        if (oram) {
            oram->serialize(os);
        }
        std::cout << "    [AVLTree::serialize] Done, pos: " << os.tellp() << std::endl;
    }
    void deserialize(std::istream& is, int maxSize, bytes<Key> key) {
        std::cout << "    [AVLTree::deserialize] Begin, pos: " << is.tellg() << std::endl;

        if (oram) {
            delete oram;
            oram = nullptr;
        }

        oram = new ORAM(maxSize, key);
        oram->deserialize(is);

        std::cout << "    [AVLTree::deserialize] Done, pos: " << is.tellg() << std::endl;
    }
};

#endif /* AVLTREE_H */




