#ifndef BID_H
#define BID_H
#include "libpq/Types.hpp"
#include <iostream>
#include <array>
using namespace std;

class Bid {
public:
    std::array< byte_t, ID_SIZE> id;
    Bid();
    Bid(int value);
    Bid(std::array< byte_t, ID_SIZE> value);
    Bid(string value);
    virtual ~Bid();
    Bid operator++ ();
    Bid& operator=(int other);
    bool operator!=(const int rhs) const ;
    bool operator!=(const Bid rhs) const ;
    bool operator==(const int rhs)const ;
    bool operator==(const Bid rhs)const ;
    Bid& operator=(std::vector<byte_t> other);
    bool operator<(const Bid& b) const ;
    bool operator>(const Bid& b) const ;
    bool operator<=(const Bid& b) const ;
    bool operator>=(const Bid& b) const ;    
    friend ostream& operator<<(ostream &o, Bid& id);

    // Bid.h 中添加
    void serialize(std::ostream& os) const {
        for (const auto& b : id) {
            os.write(reinterpret_cast<const char*>(&b), sizeof(byte_t));
        }
    }

    void deserialize(std::istream& is) {
        for (auto& b : id) {
            is.read(reinterpret_cast<char*>(&b), sizeof(byte_t));
        }
    }
};



#endif /* BID_H */

