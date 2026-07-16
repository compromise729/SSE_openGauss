#pragma once
#include "Types.hpp"
#include <map>
#include <array>

class RAMStore {
	std::vector<block> store;
	size_t size;
    size_t emptyNodes;

public:
	RAMStore(size_t num, size_t size);
	~RAMStore();

	block Read(int pos);
	void Write(int pos, block b);

	size_t GetBlockCount();
	size_t GetBlockSize();        
	bool WasSerialised();
        void ReduceEmptyNumbers();
        size_t GetEmptySize();

	// RAMStore.h 中添加
	void serialize(std::ostream& os) const {
		os.write(reinterpret_cast<const char*>(&size), sizeof(size));
		os.write(reinterpret_cast<const char*>(&emptyNodes), sizeof(emptyNodes));

		size_t blockCount = store.size();
		os.write(reinterpret_cast<const char*>(&blockCount), sizeof(blockCount));
		
		for (const auto& blk : store) {
			size_t blkSize = blk.size();
			os.write(reinterpret_cast<const char*>(&blkSize), sizeof(blkSize));
			os.write(reinterpret_cast<const char*>(blk.data()), blkSize);
		}
	}

	void deserialize(std::istream& is) {
		is.read(reinterpret_cast<char*>(&size), sizeof(size));
		is.read(reinterpret_cast<char*>(&emptyNodes), sizeof(emptyNodes));

		size_t blockCount;
		is.read(reinterpret_cast<char*>(&blockCount), sizeof(blockCount));
		store.resize(blockCount);

		for (size_t i = 0; i < blockCount; ++i) {
			size_t blkSize;
			is.read(reinterpret_cast<char*>(&blkSize), sizeof(blkSize));
			store[i].resize(blkSize);
			is.read(reinterpret_cast<char*>(store[i].data()), blkSize);
		}

		// for (int i = 0; i < 5 && i < store.size(); i++) {
        //     std::cout << "      [Deserialize] Store[" << i << "] size = " << store[i].size() << std::endl;
        // }
	}
};
