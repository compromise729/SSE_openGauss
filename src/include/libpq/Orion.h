#ifndef ORION_H
#define ORION_H
#include "OMAP.h"
#include<iostream>
using namespace std;

class Orion {
private:
    bool useHDD;
    map<Bid,string > setupPairs1;
    map<Bid,string > setupPairs2;
    OMAP* srch,*updt;
    map<string, int> UpdtCnt;
    map<string, int> LastIND;        
    
public:
    Bid createBid(string keyword,int number);
    void insert(string keyword, int ind);
    void setupInsert(string keyword, int ind);
    void remove(string keyword, int ind);
    void setupRemove(string keyword, int ind);
    vector<int> search(string keyword);
    Orion(bool useHDD,int maxSize);    
    virtual ~Orion();
    void beginSetup();
    void endSetup();


    void serialize(std::ostream& os) const {
        std::cout << "[Serialize] Start at position: " << os.tellp() << std::endl;

        os.write(reinterpret_cast<const char*>(&useHDD), sizeof(useHDD));
        std::cout << "[Serialize] useHDD, pos: " << os.tellp() << std::endl;

        size_t s1_size = setupPairs1.size();
        os.write(reinterpret_cast<const char*>(&s1_size), sizeof(s1_size));
        std::cout << "[Serialize] setupPairs1 size: " << s1_size << ", pos: " << os.tellp() << std::endl;

        for (std::map<Bid, std::string>::const_iterator it = setupPairs1.begin(); it != setupPairs1.end(); ++it) {
            const Bid& bid = it->first;
            const std::string& val = it->second;
            bid.serialize(os);
            size_t len = val.size();
            os.write(reinterpret_cast<const char*>(&len), sizeof(len));
            os.write(val.data(), len);
            std::cout << "[Serialize] setupPairs1 item, len: " << len << ", pos: " << os.tellp() << std::endl;
        }

        size_t s2_size = setupPairs2.size();
        os.write(reinterpret_cast<const char*>(&s2_size), sizeof(s2_size));
        std::cout << "[Serialize] setupPairs2 size: " << s2_size << ", pos: " << os.tellp() << std::endl;

        for (std::map<Bid, std::string>::const_iterator it = setupPairs2.begin(); it != setupPairs2.end(); ++it) {
            const Bid& bid = it->first;
            const std::string& val = it->second;
            bid.serialize(os);
            size_t len = val.size();
            os.write(reinterpret_cast<const char*>(&len), sizeof(len));
            os.write(val.data(), len);
            std::cout << "[Serialize] setupPairs2 item, len: " << len << ", pos: " << os.tellp() << std::endl;
        }

        size_t cnt_size = UpdtCnt.size();
        os.write(reinterpret_cast<const char*>(&cnt_size), sizeof(cnt_size));
        std::cout << "[Serialize] UpdtCnt size: " << cnt_size << ", pos: " << os.tellp() << std::endl;

        for (std::map<std::string, int>::const_iterator it = UpdtCnt.begin(); it != UpdtCnt.end(); ++it) {
            const std::string& key = it->first;
            int val = it->second;
            size_t len = key.size();
            os.write(reinterpret_cast<const char*>(&len), sizeof(len));
            os.write(key.data(), len);
            os.write(reinterpret_cast<const char*>(&val), sizeof(val));
            std::cout << "[Serialize] UpdtCnt[" << key << "] = " << val << ", pos: " << os.tellp() << std::endl;
        }

        size_t ind_size = LastIND.size();
        os.write(reinterpret_cast<const char*>(&ind_size), sizeof(ind_size));
        std::cout << "[Serialize] LastIND size: " << ind_size << ", pos: " << os.tellp() << std::endl;

        for (std::map<std::string, int>::const_iterator it = LastIND.begin(); it != LastIND.end(); ++it) {
            const std::string& key = it->first;
            int val = it->second;
            size_t len = key.size();
            os.write(reinterpret_cast<const char*>(&len), sizeof(len));
            os.write(key.data(), len);
            os.write(reinterpret_cast<const char*>(&val), sizeof(val));
            std::cout << "[Serialize] LastIND[" << key << "] = " << val << ", pos: " << os.tellp() << std::endl;
        }

        std::cout << "[Serialize] Writing srch at: " << os.tellp() << std::endl;
        srch->serialize(os);

        std::cout << "[Serialize] Writing updt at: " << os.tellp() << std::endl;
        updt->serialize(os);

        std::cout << "[Serialize] Done! Final pos: " << os.tellp() << std::endl;
    }


    void deserialize(std::istream& is, int maxSize) {
        std::cout << "[Deserialize] Begin at position: " << is.tellg() << std::endl;

        is.read(reinterpret_cast<char*>(&useHDD), sizeof(useHDD));
        std::cout << "[Deserialize] useHDD: " << useHDD << ", position: " << is.tellg() << std::endl;

        // setupPairs1
        size_t s1_size;
        is.read(reinterpret_cast<char*>(&s1_size), sizeof(s1_size));
        std::cout << "[Deserialize] setupPairs1 size: " << s1_size << ", position: " << is.tellg() << std::endl;

        setupPairs1.clear();
        for (size_t i = 0; i < s1_size; ++i) {
            Bid bid;
            bid.deserialize(is);
            size_t len;
            is.read(reinterpret_cast<char*>(&len), sizeof(len));

            if (len > 1 << 20) throw std::runtime_error("Invalid string length in setupPairs1");

            std::string val(len, '\0');
            is.read(&val[0], len);
            setupPairs1[bid] = val;

            std::cout << "[Deserialize] setupPairs1[" << i << "] = " << val << ", pos: " << is.tellg() << std::endl;
        }

        // setupPairs2
        size_t s2_size;
        is.read(reinterpret_cast<char*>(&s2_size), sizeof(s2_size));
        std::cout << "[Deserialize] setupPairs2 size: " << s2_size << ", position: " << is.tellg() << std::endl;

        setupPairs2.clear();
        for (size_t i = 0; i < s2_size; ++i) {
            Bid bid;
            bid.deserialize(is);
            size_t len;
            is.read(reinterpret_cast<char*>(&len), sizeof(len));

            if (len > 1 << 20) throw std::runtime_error("Invalid string length in setupPairs2");

            std::string val(len, '\0');
            is.read(&val[0], len);
            setupPairs2[bid] = val;

            std::cout << "[Deserialize] setupPairs2[" << i << "] = " << val << ", pos: " << is.tellg() << std::endl;
        }

        // UpdtCnt
        size_t cnt_size;
        is.read(reinterpret_cast<char*>(&cnt_size), sizeof(cnt_size));
        std::cout << "[Deserialize] UpdtCnt size: " << cnt_size << ", position: " << is.tellg() << std::endl;

        UpdtCnt.clear();
        for (size_t i = 0; i < cnt_size; ++i) {
            size_t len;
            is.read(reinterpret_cast<char*>(&len), sizeof(len));

            if (len > 1000) throw std::runtime_error("Invalid key length in UpdtCnt");

            std::string keyStr(len, '\0');
            is.read(&keyStr[0], len);
            int val;
            is.read(reinterpret_cast<char*>(&val), sizeof(val));
            UpdtCnt[keyStr] = val;

            std::cout << "[Deserialize] UpdtCnt[" << keyStr << "] = " << val << ", pos: " << is.tellg() << std::endl;
        }

        // LastIND
        size_t ind_size;
        is.read(reinterpret_cast<char*>(&ind_size), sizeof(ind_size));
        std::cout << "[Deserialize] LastIND size: " << ind_size << ", position: " << is.tellg() << std::endl;

        LastIND.clear();
        for (size_t i = 0; i < ind_size; ++i) {
            size_t len;
            is.read(reinterpret_cast<char*>(&len), sizeof(len));

            if (len > 1000) throw std::runtime_error("Invalid key length in LastIND");

            std::string keyStr(len, '\0');
            is.read(&keyStr[0], len);
            int val;
            is.read(reinterpret_cast<char*>(&val), sizeof(val));
            LastIND[keyStr] = val;

            std::cout << "[Deserialize] LastIND[" << keyStr << "] = " << val << ", pos: " << is.tellg() << std::endl;
        }

        // 删除旧对象
        if (srch) delete srch;
        if (updt) delete updt;

        bytes<Key> key1{0};
        bytes<Key> key2{1};
        srch = new OMAP(maxSize * 4, key1);
        updt = new OMAP(maxSize * 4, key2);

        std::cout << "[Deserialize] Deserializing srch, pos: " << is.tellg() << std::endl;
        srch->deserialize(is, maxSize * 4, key1);

        std::cout << "[Deserialize] Deserializing updt, pos: " << is.tellg() << std::endl;
        updt->deserialize(is, maxSize * 4, key2);

        std::cout << "[Deserialize] Done!"<< is.tellg() << std::endl;
    }

};
extern Orion* Orion_server;
#endif /* ORION_H */

