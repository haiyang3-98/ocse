#include "Lpc.h"
#include <stdio.h>
#include <string.h>	// memcpy

Lpc::Lpc() {
    uint32_t i;

    std::vector<uint8_t> x32k(32);
    std::vector<uint8_t> x256(128);
    std::vector<uint8_t> x64(4);
    printf("Lpc: allocating LPC memory\n");
    uint8_t *lpc_memory;
    lpc_memory = new uint8_t [0x1000];

    // initialize lpc memory
    for(i=0; i<0x1000; i++)
	lpc_memory[i] = 0;
}

// address must be 64 byte aligned
uint8_t
Lpc::lpc_addr_exist(uint64_t addr) {
    
    if(addr_list.empty()) {
	printf("Lpc: add address to lpc list entry\n");
	addr_list.push_back(addr);
	return 0;
    }
    else {
	for(it_list=addr_list.begin(); it_list != addr_list.end(); it_list++) {
	    if(*it_list != addr) {
		continue;
	    }
	    else {
		printf("Lpc: address exist\n");
		return 1;
	    }
	}
    }
    return 0;
}

void
Lpc::read_lpc_mem(uint64_t addr, uint16_t size, uint8_t *data) {
    uint32_t i;
    printf("Lpc: read memory at addr = 0x%lx\n", addr);
    if(lpc_addr_exist(addr)) {
	addr = addr & 0x0000000000000FC0;
	for(i=0; i< size; i++) {
	    data[i] = lpc_memory[addr+i];
	}
	//memcpy(&data, (uint8_t*)addr, size);
    }
    else {
	printf("Lpc: no data at the addr requested\n");
    }
}

void
Lpc::write_lpc_mem(uint64_t addr, uint16_t size, uint8_t *data) {
    uint32_t i;

    printf("Lpc: write memory at addr = 0x%lx and size = 0x%2x data addr = 0x%x \n", addr, size, data);
    if(lpc_addr_exist(addr)) {
	printf("Lpc: will overwrite data at this address\n");
    }
    addr = addr & 0x0000000000000FC0;
    printf("Lpc: lpc_memory = 0x%x\n", lpc_memory);
    printf("Lpc: data = 0x");
    for(i=0; i< size; i++) {
	lpc_memory[addr+i] = data[i];
	printf("%02x", data[i]);
    }
    //memcpy((uint8_t*)lpc_memory[addr], data, size);
}

Lpc::~Lpc() {
    delete[] lpc_memory;
}

