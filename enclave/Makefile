# Enclave comparison build (Intel SGX SDK, hardware mode). Run `make` in the
# repo root first: the enclave links the generated ROB/PRF kernels from
# ../build. Flags follow the SDK's SampleEnclave.
#
#   hw3_sgx + enclave.signed.so   experiment core inside an enclave
#   hw3_native                    the same core in a normal process
SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
SGX_DEBUG ?= 1
include $(SGX_SDK)/buildenv.mk

SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
SGX_SIGNER := $(SGX_SDK)/bin/x64/sgx_sign
SGX_EDGER8R := $(SGX_SDK)/bin/x64/sgx_edger8r
SGX_TRUSTED_LIBRARY_PATH := $(SGX_LIBRARY_PATH)
Trts_Library_Name := sgx_trts
Service_Library_Name := sgx_tservice
Urts_Library_Name := sgx_urts

# -O2 everywhere: timed code is assembly, and both builds must match.
COMMON  := -m64 -O2 -g -Wall -Wextra
INC     := -I. -I../common -I../exp/rob_prf
KERNELS := ../build/rob_prf_kernels.S ../build/rob_prf_table.c ../exp/tlb/tlb_chase.S
KEY     := enclave_private.pem

ENCLAVE_CFLAGS := $(COMMON) $(INC) -I$(SGX_SDK)/include -I$(SGX_SDK)/include/tlibc \
	-nostdinc -fvisibility=hidden -fpie -ffunction-sections -fdata-sections \
	-fstack-protector-strong $(MITIGATION_CFLAGS)
ENCLAVE_LDFLAGS := $(MITIGATION_LDFLAGS) -Wl,-z,relro,-z,now,-z,noexecstack \
	-Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles -L$(SGX_TRUSTED_LIBRARY_PATH) \
	-Wl,--whole-archive -l$(Trts_Library_Name) -Wl,--no-whole-archive \
	-Wl,--start-group -lsgx_tstdc -lsgx_tcrypto -l$(Service_Library_Name) -Wl,--end-group \
	-Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined \
	-Wl,-pie,-eenclave_entry -Wl,--export-dynamic \
	-Wl,--defsym,__ImageBase=0 -Wl,--gc-sections -Wl,--version-script=Enclave.lds

all: hw3_sgx enclave.signed.so hw3_native

hw3_t.c hw3_t.h: hw3.edl
	$(SGX_EDGER8R) --trusted hw3.edl --search-path $(SGX_SDK)/include
hw3_u.c hw3_u.h: hw3.edl
	$(SGX_EDGER8R) --untrusted hw3.edl --search-path $(SGX_SDK)/include

enclave.so: hw3_t.c hw3_t.h enclave_main.c core.c core.h $(KERNELS) Enclave.lds
	$(CC) $(ENCLAVE_CFLAGS) -o $@ hw3_t.c enclave_main.c core.c $(KERNELS) $(ENCLAVE_LDFLAGS)

$(KEY):
	openssl genrsa -out $@ -3 3072

enclave.signed.so: enclave.so Enclave.config.xml $(KEY)
	$(SGX_SIGNER) sign -key $(KEY) -enclave enclave.so -out $@ -config Enclave.config.xml

hw3_sgx: app.c hw3_u.c hw3_u.h ../common/hw3_common.c
	$(CC) $(COMMON) $(INC) -I$(SGX_SDK)/include -DUSE_SGX -o $@ app.c hw3_u.c \
		../common/hw3_common.c -L$(SGX_LIBRARY_PATH) -l$(Urts_Library_Name) -lpthread

hw3_native: app.c core.c core.h ../common/hw3_common.c $(KERNELS)
	$(CC) $(COMMON) $(INC) -o $@ app.c core.c ../common/hw3_common.c $(KERNELS)

clean:
	rm -f hw3_sgx hw3_native enclave.so enclave.signed.so hw3_t.* hw3_u.*

.PHONY: all clean
