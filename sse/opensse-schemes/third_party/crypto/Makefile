# Define the build directory for the third-party library
THIRD_PARTY_DIR = /opt/og/openGauss-server/sse/opensse-schemes/third_party/crypto
THIRD_PARTY_BUILD_DIR = $(THIRD_PARTY_DIR)/build

# Ensure third-party library is built before building PostgreSQL
all: third_party_build

third_party_build:
	cd $(THIRD_PARTY_DIR) && mkdir -p $(THIRD_PARTY_BUILD_DIR)
	cd $(THIRD_PARTY_BUILD_DIR) && cmake .. \
		-DCMAKE_C_COMPILER=$(GCC_PATH)/gcc/bin/gcc \
		-DCMAKE_CXX_COMPILER=$(GCC_PATH)/gcc/bin/g++ \
		-DCMAKE_CXX_FLAGS=-D_GLIBCXX_USE_CXX11_ABI=0 \
		-Drelic_DIR=/opt/og/fides_relic/cmake \
		-DRLC_INCLUDE_DIR=/opt/og/fides_relic/include \
		-DRLC_LIBRARY=/opt/og/fides_relic/lib/librelic.so
	$(MAKE) -C $(THIRD_PARTY_BUILD_DIR)

install:
	
# Clean third-party build directory
clean:
	rm -rf $(THIRD_PARTY_BUILD_DIR)