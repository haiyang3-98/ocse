/*
 * Copyright 2014,2017 International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MMIO_H_
#define _MMIO_H_

#include <pthread.h>
#include <stdint.h>
#include <stdint.h>
#include <stdio.h>

#include "client.h"
#include "../common/tlx_interface.h"
#include "../common/utils.h"

#define PROG_MODEL_MASK 0x7fff
#define PROG_MODEL_DEDICATED 0x0010
#define PROG_MODEL_DIRECTED 0x0004
#define MMIO_FULL_RANGE 0x4000000
#define PSA_MASK             0x00FFFFFFFFFFFFFFL
#define PSA_REQUIRED         0x0100000000000000L
#define PROCESS_PSA_REQUIRED 0x0200000000000000L
#define FOUR_K 0x1000
#define CXL_MMIO_BIG_ENDIAN 0x1
#define CXL_MMIO_LITTLE_ENDIAN 0x2
#define CXL_MMIO_HOST_ENDIAN 0x3
#define CXL_MMIO_ENDIAN_MASK 0x3


struct mmio_event {
	uint32_t rnw;
	uint32_t dw;    // TODO remove this ?
	uint32_t eb_rd; //TODO remove this
	uint32_t cfg;
	uint64_t cmd_data;
	uint64_t cmd_PA;
	uint16_t cmd_CAPPtag;
	uint8_t cmd_opcode;
	uint8_t cmd_pL;
	uint8_t cmd_TorR;  //may not need this
	uint8_t cmd_rd_cnt;
	enum ocse_state state;
	struct mmio_event *_next;
};


struct afu_cfg_sp {
        uint16_t cr_device;
        uint16_t cr_vendor;
        uint32_t PASID_CP;
        uint32_t PASID_CTL_STS;
        uint32_t OCAPI_TL_CP;
        uint32_t OCAPI_TL_REVID;
        uint32_t OCAPI_TL_ACTAG;
        uint32_t OCAPI_TL_MAXAFU;
        uint32_t OCAPI_TL_TMP_CFG;
        uint32_t OCAPI_TL_TX_RATE;
        uint32_t AFU_INFO_CP;
        uint32_t AFU_INFO_REVID;
        uint32_t AFU_INFO_INDEX;
        uint32_t AFU_CTL_CP;
        uint32_t AFU_CTL_REVID;
        uint32_t AFU_CTL_EN_RST_INDEX;
        uint32_t AFU_CTL_PASID_LEN;
        uint32_t AFU_CTL_PASID_BASE;
        uint32_t AFU_CTL_INTS_PER_PASID;
        uint32_t AFU_DESC_MMIO_SPACE;
        uint32_t AFU_DESC_MMIO_PP_OFFSET_BAR;
        uint32_t AFU_DESC_MMIO_PP_STRIDE;
//	uint16_t num_ints_per_process;
//	uint16_t num_of_processes;
//	uint16_t num_of_afu_CRs;
//	uint16_t req_prog_model;
//	uint64_t reserved1;
//	uint64_t reserved2;
//	uint64_t reserved3;
//	uint64_t AFU_CR_len;
//	uint64_t AFU_CR_offset;
//	uint64_t PerProcessPSA;
//	uint64_t PerProcessPSA_offset;
//	uint64_t AFU_EB_len;
//	uint64_t AFU_EB_offset;
};

struct mmio {
	struct AFU_EVENT *afu_event;
	struct afu_cfg_sp cfg;
	struct mmio_event *list;
	char *afu_name;
	FILE *dbg_fp;
	uint8_t dbg_id;
	uint32_t flags;
	int timeout;
};

struct mmio *mmio_init(struct AFU_EVENT *afu_event, int timeout, char *afu_name,
		       FILE * dbg_fp, uint8_t dbg_id);

int read_afu_config(struct mmio *mmio, pthread_mutex_t * lock);

struct mmio_event *add_mmio(struct mmio *mmio, uint32_t rnw, uint32_t dw,
			    uint64_t addr, uint64_t data);

void send_mmio(struct mmio *mmio);

void handle_mmio_ack(struct mmio *mmio, uint32_t parity_enabled);

void handle_mmio_map(struct mmio *mmio, struct client *client);

struct mmio_event *handle_mmio(struct mmio *mmio, struct client *client,
			       int rnw, int dw, int eb_rd);

struct mmio_event *handle_mmio_done(struct mmio *mmio, struct client *client);

//int dedicated_mode_support(struct mmio *mmio);

//int directed_mode_support(struct mmio *mmio);

#endif				/* _MMIO_H_ */
