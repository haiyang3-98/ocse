#include "AFU.h"

#include <string>
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <time.h>

using std::string;
using std::cout;
using std::endl;
using std::vector;

#define GLOBAL_CONFIG_OFFSET 0x400
#define CONTEXT_SIZE 0x400
#define CONTEXT_MASK (CONTEXT_SIZE - 1)

uint8_t memory[256];
uint8_t next_cmd = 0;
uint8_t read_resp_completed = 0;
uint8_t write_resp_completed = 0;
uint8_t cmd_ready = 1;
uint8_t status_data[256];
uint8_t status_resp_valid = 0;
uint8_t status_updated = 0;
uint8_t insert_cycle = 0;
uint8_t afu_enable_reset = 0;

AFU::AFU (int port, string filename, bool parity, bool jerror):
    descriptor (filename),
    context_to_mc ()
{

    // initializes AFU socket connection as server
    if (tlx_serv_afu_event (&afu_event, port) == TLX_BAD_SOCKET)
        error_msg ("AFU: unable to create socket");

    if (jerror)
	set_jerror_not_run = true;
    else
	set_jerror_not_run = false;
    set_seed ();

    state = IDLE;
    config_state = IDLE;
    mem_state = IDLE;
    debug_msg("AFU: Set AFU and CONFIG state = IDLE");
    afu_event.afu_tlx_resp_initial_credit = MAX_AFU_TLX_RESP_CREDITS; 
    afu_event.afu_tlx_cmd_initial_credit = MAX_AFU_TLX_CMD_CREDITS;
    afu_tlx_send_initial_credits(&afu_event, MAX_AFU_TLX_CMD_CREDITS,
		10, MAX_AFU_TLX_RESP_CREDITS);
    debug_msg("AFU: Send initial afu cmd and resp credits to ocse");
    reset ();
}

bool 
AFU::afu_is_enabled()
{
    uint32_t  enable;
    enable = descriptor.get_vsec_reg(0x50c);
    if(enable & 0x01000000)
	return true;
    else
	return false;	
}

bool
AFU::afu_is_reset()
{
    uint32_t reset;
 
    reset = descriptor.get_afu_desc_reg(0x50c);
    if(reset & 0x00800000)
	return true;
    else
	return false;   
}
void
AFU::start ()
{
    uint32_t cycle = 0;
    uint8_t  initial_credit_flag = 0;

    while (1) {
        fd_set watchset;

        FD_ZERO (&watchset);
        FD_SET (afu_event.sockfd, &watchset);
        select (afu_event.sockfd + 1, &watchset, NULL, NULL, NULL);

	// check socket if there are new events from ocse to process
	printf("getting tlx events\n");
        int rc = tlx_get_tlx_events (&afu_event);

        //info_msg("Cycle: %d", cycle);
        ++cycle;

	// connection dropped
        if (rc < 0) {
            info_msg ("AFU: connection lost");
            break;
        }

	// get TLX initial cmd and data credits run once
	if(initial_credit_flag == 0) {
	    debug_msg("AFU: afu read initial credit");
	    if(tlx_afu_read_initial_credits(&afu_event, &tlx_afu_cmd_max_credit,
		&tlx_afu_data_max_credit) != TLX_SUCCESS) {
		error_msg("AFU: Failed tlx_afu_read_initial_credits");
	    }
	    TagManager::reset_tlx_credit(tlx_afu_cmd_max_credit, tlx_afu_data_max_credit);
	    info_msg("AFU: Receive TLX cmd and data initial credits");
    	    debug_msg("AFU:  tlx_afu_cmd_max_credit = %d", tlx_afu_cmd_max_credit);
    	    debug_msg("AFU:  tlx_afu_data_max_credit = %d", tlx_afu_data_max_credit);
	    initial_credit_flag = 1;
	}

	// no new events to be processed
        if (rc <= 0)		
            continue;
	
	// Return TLX credit
	if(afu_event.tlx_afu_resp_credit) {
	    TagManager::release_tlx_credit(RESP_CREDIT);
	    afu_event.tlx_afu_resp_credit = 0;
	}
	if(afu_event.tlx_afu_resp_data_credit) {
	    TagManager::release_tlx_credit(RESP_DATA_CREDIT);
	    afu_event.tlx_afu_resp_data_credit = 0;
	}
	if(afu_event.tlx_afu_cmd_credit) {
	    TagManager::release_tlx_credit(CMD_CREDIT);
	    afu_event.tlx_afu_cmd_credit = 0;
	}
	if(afu_event.tlx_afu_cmd_data_credit) {
	    TagManager::release_tlx_credit(CMD_DATA_CREDIT);
	    afu_event.tlx_afu_cmd_data_credit = 0;
	}

	// process tlx commands
	if (afu_event.tlx_afu_cmd_valid) {
	    debug_msg("AFU: Received TLX command 0x%x", afu_event.tlx_afu_cmd_opcode);
	    debug_msg("AFU: Process TLX command");
	    resolve_tlx_afu_cmd();
	    afu_event.afu_tlx_cmd_credit = 1;	// return TLX cmd credit
	}
	// process tlx response
	if (afu_event.tlx_afu_resp_valid) {
	    debug_msg("AFU: Received TLX response 0x%x", afu_event.tlx_afu_resp_opcode);
	    resolve_tlx_afu_resp();
	    afu_event.afu_tlx_resp_credit = 1;	// return TLX resp credit
	    afu_event.tlx_afu_resp_valid = 0;
	}
	// process tlx config response
	if (afu_event.tlx_cfg_resp_ack) {
	    debug_msg("AFU: Received TLX config response 0x%x", afu_event.tlx_afu_resp_opcode);
	    resolve_tlx_afu_resp();
	    afu_event.cfg_tlx_credit_return = 1;	
	    afu_event.tlx_cfg_resp_ack = 0;
	}
	// process tlx response data
	if(afu_event.tlx_afu_resp_data_valid && mem_state == WAITING_FOR_DATA) {
	    debug_msg("AFU: Received TLX response data");
	    resolve_tlx_afu_resp();
	    afu_event.tlx_afu_resp_data_credit = 1;	// return TLX resp data credit
	    afu_event.tlx_afu_resp_data_valid = 0;
	}
	// configuration write response
	if (afu_event.tlx_afu_cmd_data_valid && config_state == READY) {
	    info_msg("AFU: calling tlx_afu_config_write");
	    tlx_afu_config_write();
	}

	// partial write mem response
	if (afu_event.tlx_afu_cmd_data_valid && mem_state == READY) {
	    info_msg("AFU: calling tlx_pr_wr_mem");
	    tlx_pr_wr_mem();
 	}

	//printf("checking afu reset bit\n");
	//  reset AFU
	if(afu_enable_reset) {
	    if(afu_is_reset()) {
	    	debug_msg("AFU is resetting");
		afu_enable_reset = 0;
	    }
	}
	// enable AFU
	//if(afu_is_enabled() && state == IDLE) {
	if(state == IDLE) {
	    if(afu_enable_reset) {
	    	printf("checking afu enable bit\n");
	    	if(afu_is_enabled()) {
	    	    debug_msg("AFU is enabled");
	    	    debug_msg("AFU: set state = READY");
	    	    state = READY;
		    afu_enable_reset = 0;
	        }
	    }
	}
	// get machine context and create new MachineController
	else if(state == READY) {
	    if(get_machine_context()) {
		printf("AFU: request afu assign actag\n");
		request_assign_actag();
	    	printf("AFU: set state = RUNNING\n");
	    	state = RUNNING;
	    }
	}
  	
        // generate commands, initial read_resp_completed=0; write_resp_completed=0
	// initial cmd_ready=1; next_cmd=0;
        else if (state == RUNNING) {
	    if((read_resp_completed ||  write_resp_completed) &&
		!(next_cmd) && !(next_cmd) && !(cmd_ready)) {
		printf("AFU: writing app status\n");
	   	if(!insert_cycle) {
	  	    write_app_status(status_address, 0x00);
		    insert_cycle = 1;
		}
		else {
		    printf("insert cycle\n");
		    insert_cycle = 0;
		    read_resp_completed = 0;
		    write_resp_completed = 0;
		    next_cmd = 1;
		}
	    }
	    else if(next_cmd) {
		debug_msg("AFU: reading app status");
		read_app_status(status_address);
		debug_msg("AFU: status data = 0x%x", status_data[0]);
		if(status_data[0] == 0xff) {
		    debug_msg("AFU: get next cmd from app");
		    next_cmd = 0;
		    cmd_ready = 1;
		    get_machine_context();
		}
		else {
		    debug_msg("AFU: waiting for new cmd from app");
		}
	    }
            else if(cmd_ready) {
		cmd_ready = 0;
		if(context_to_mc.size () != 0) {
                	std::map < uint16_t, MachineController * >::iterator prev =
                    	highest_priority_mc;
                    do {
                    if(highest_priority_mc == context_to_mc.end ())
                        highest_priority_mc = context_to_mc.begin ();
		// calling MachineController send command
                    	if(highest_priority_mc->second->send_command(&afu_event, cycle)) {
                        	++highest_priority_mc;
                        	break;
                    	    }
                    //++highest_priority_mc;
                	} while (++highest_priority_mc != prev);
		}
            }
        }
        else if (state == RESET) {
	    debug_msg("AFU: resetting");
	    debug_msg("AFU: set AFU state = READY");
      	    reset ();
	    state = READY;
	}
        else if (state == WAITING_FOR_LAST_RESPONSES) {
            //debug_msg("AFU: waiting for last responses");
            bool all_machines_completed = true;
	    uint32_t  response;
	    uint32_t  offset;
	  
	    response = 0x00000000;
	    offset  = 0x1008 + 0x1000 * afu_event.afu_tlx_cmd_pasid;
	    descriptor.set_mmio_mem(offset, (char*)&response, 4);
            for (std::map < uint16_t, MachineController * >::iterator it =
                        context_to_mc.begin (); it != context_to_mc.end (); ++it)
            {
                if (!(it->second)->all_machines_completed ())
                    all_machines_completed = false;
            }

            if (all_machines_completed) {
                debug_msg ("AFU: machine completed");

                reset_machine_controllers ();
              
                state = HALT;
		debug_msg("AFU: state = HALT");
            }
        }
    }
}

AFU::~AFU ()
{
    // close socket connection
    tlx_close_afu_event (&afu_event);

    for (std::map < uint16_t, MachineController * >::iterator it =
                context_to_mc.begin (); it != context_to_mc.end (); ++it)
        delete it->second;

    context_to_mc.clear ();
}


void
AFU::reset ()
{
    for (uint32_t i = 0; i < 3; ++i)
        global_configs[i] = 0;

    reset_delay = 0;

    reset_machine_controllers ();
}

void
AFU::reset_machine_controllers ()
{
    TagManager::reset ();

    for (std::map < uint16_t, MachineController * >::iterator it =
                context_to_mc.begin (); it != context_to_mc.end (); ++it)
        delete it->second;

    context_to_mc.clear ();

}

// get machine context from mmio and create new MachineController
bool 
AFU::get_machine_context()
{
    MachineController *mc = NULL;
    uint64_t  data;
    uint8_t  size = 8;
    uint16_t  context, machine_number, i;
    uint32_t  mmio_base;

    machine_number = 0;
    for(context=1; context<4; context++) {
	descriptor.get_mmio_mem(0x1000*context+machine_number, (char*)&data, size);
	if(data) {
	    mmio_base = 0x1000*context + machine_number;
	    printf("context = %d  machine = %d\n", context-1, machine_number);
	    context = (uint16_t)((data & 0x00000000FFFF0000LL) >> 32);
	    debug_msg("AFU: get cmd pasid");
	    afu_event.afu_tlx_cmd_pasid = context;
	    context_to_mc[context] = new MachineController(context);
	    highest_priority_mc = context_to_mc.end();
	    mc = context_to_mc[context];
	    printf("AFU: context_to_mc = 0x%p\n", mc);
	    for(i=0; i< 4; i++) {
		descriptor.get_mmio_mem(mmio_base+i*8, (char*)&data, size);
		if(i==1) {
		    status_address = (uint8_t *)(data >> 32);
		    debug_msg("AFU: get status address = 0x%p", status_address);
		}
		mc->change_machine_config(i, machine_number, data);
	    }
	    return true;
	}
    }
    return false;		
}

void
AFU::request_assign_actag()
{
    uint8_t ea[12];
    uint32_t afutag;

    TagManager::request_tag(&afutag);

    afu_event.afu_tlx_cmd_actag = 0x01;
    afu_event.afu_tlx_cmd_afutag = afutag;
    printf("AFU: afu_tag = 0x%x\n", afutag);
    printf("AFU: assign actag BDF = 0x%x PASID = 0x%x\n", afu_event.afu_tlx_cmd_bdf,
    afu_event.afu_tlx_cmd_pasid);
    if(afu_tlx_send_cmd(&afu_event,
		 AFU_CMD_ASSIGN_ACTAG, afu_event.afu_tlx_cmd_actag,
  	 	 0, ea, afu_event.afu_tlx_cmd_afutag, 1, 3,
#ifdef TLX4
  		 0,     /* 1 bit ordered segment CAPI 4 */
#endif
  	 	 0, 0, 0, afu_event.afu_tlx_cmd_bdf,
 		 afu_event.afu_tlx_cmd_pasid, 0) != TLX_SUCCESS) {
	    printf("FAILED: request_assign_actag\n");
	}
	else {
	    printf("PASS: request_assign_actag\n");
	    //afu_event.afu_tlx_cmd_valid = 0;
	}
}

// process commands from ocse to AFU
void 
AFU::resolve_tlx_afu_cmd()
{
    uint8_t tlx_cmd_opcode;
    uint16_t cmd_capptag;
    uint8_t  cmd_dl;
    uint8_t  cmd_pl;
    uint64_t cmd_be;
    uint8_t  cmd_end;
    uint8_t  cmd_t;
#ifdef	TLX4
    uint8_t  cmd_flag;
    uint8_t  cmd_os;
#endif
    uint64_t  cmd_pa;
    
    if (tlx_afu_read_cmd(&afu_event, &tlx_cmd_opcode, &cmd_capptag, 
		&cmd_dl, &cmd_pl, &cmd_be, &cmd_end, &cmd_t, 
#ifdef	TLX4
	&cmd_os, &cmd_flag,
#endif
		&cmd_pa) != TLX_SUCCESS) {
	error_msg("Failed: tlx_afu_read_cmd");
    }
    
    afu_event.afu_tlx_resp_capptag = cmd_capptag;
    info_msg("AFU:resolve_tlx_afu_cmd");
    info_msg("cmd_opcode = 0x%x", tlx_cmd_opcode);
    info_msg("cmd_pa = 0x%08lx", cmd_pa);
    info_msg("cmd_capptag = 0x%x", cmd_capptag);
    info_msg("cmd_pl = 0x%x", cmd_pl);

    switch (tlx_cmd_opcode) {
	case TLX_CMD_NOP:
	case TLX_CMD_XLATE_DONE:
	case TLX_CMD_RETURN_ADR_TAG:
	case TLX_CMD_INTRP_RDY:
	case TLX_CMD_RD_MEM:
	case TLX_CMD_PR_RD_MEM:
	    debug_msg("AFU: pr_rd_mem cmd:");
	    tlx_pr_rd_mem();
	    break;
	case TLX_CMD_AMO_RD:
	case TLX_CMD_AMO_RW:
	case TLX_CMD_AMO_W:
	case TLX_CMD_WRITE_MEM:
	case TLX_CMD_WRITE_MEM_BE:
	case TLX_CMD_WRITE_META:
	case TLX_CMD_PR_WR_MEM:
	    debug_msg("AFU: pr_wr_mem cmd:");
	    tlx_pr_wr_mem();
	    break;
	case TLX_CMD_FORCE_EVICT:
	case TLX_CMD_FORCE_UR:
	case TLX_CMD_WAKE_AFU_THREAD:
	case TLX_CMD_CONFIG_READ:
	    info_msg("AFU: Configuration Read command");
	    if(afu_event.tlx_afu_cmd_t == 0) {
		info_msg("AFU: calling tlx_afu_config_read");
		tlx_afu_config_read();
	    }
	    else if(afu_event.tlx_afu_cmd_t == 1) {
		
	    }
	    break;
	case TLX_CMD_CONFIG_WRITE:
	    info_msg("AFU: Configuration Write command");
	    if(afu_event.tlx_afu_cmd_t == 0) {
		info_msg("AFU: calling tlx_afu_config_write");
		tlx_afu_config_write();
	    }
	    else {
		// do configuration write
	    }
	    break;
	default:
	    break;
    }
}

// process responses from ocse to AFU
void
AFU::resolve_tlx_afu_resp()
{
    uint8_t tlx_resp_opcode;
    uint16_t resp_afutag;
    uint8_t  resp_code;
    uint8_t  resp_pg_size;
    uint8_t  resp_resp_dl;
#ifdef	TLX4
    uint32_t resp_host_tag;
    uint8_t  resp_cache_state;
#endif
    uint8_t  resp_dp;
    uint32_t resp_addr_tag;
    uint8_t  cmd_rd_req, cmd_rd_cnt;
    uint8_t  resp_data_bdi;
    uint8_t  cdata_bad;
    uint8_t  i;

    tlx_resp_opcode = 0;
    resp_data_bdi = afu_event.tlx_afu_resp_data_bdi;

    if(mem_state == WAITING_FOR_DATA) {
	debug_msg("AFU: calling tlx_afu_read_resp_data");
	if(status_resp_valid) {
	    debug_msg("AFU: read_resp_data for status_resp_valid");
	    status_resp_valid = 0;
	    tlx_afu_read_resp_data(&afu_event, &resp_data_bdi, status_data);
	}
	else {
	    debug_msg("AFU: read_resp_data for memory");
	    tlx_afu_read_resp_data(&afu_event, &resp_data_bdi, memory);
	}
	debug_msg("AFU: set mem_state = IDLE");
	mem_state = IDLE;
//	debug_msg("AFU: set state = READY");
//	state = READY;
	printf("memory: = 0x");
	for(i=0; i<64; i++)
	    printf("%02x", (uint8_t)memory[i]);
	printf("\n");
    }
    else {
    	tlx_afu_read_resp(&afu_event, &tlx_resp_opcode, &resp_afutag, 
		&resp_code, &resp_pg_size, &resp_resp_dl,
#ifdef	TLX4
		&resp_host_tag, &resp_cache_state,
#endif
		&resp_dp, &resp_addr_tag);
 
	//next_cmd = 1;
	read_resp_completed = 1;
    }

    switch (tlx_resp_opcode) {
	case TLX_RSP_NOP:
	    break;
	case TLX_RSP_RET_TLX_CREDITS:
	    break;
	case TLX_RSP_TOUCH_RESP:
	    break;
	case TLX_RSP_READ_RESP:
	    debug_msg("AFU: read_resp: calling afu_tlx_resp_data_read_req");
	    cmd_rd_req = 0x1;	
	    cmd_rd_cnt = 0x1; 	// 0=512B, 1=64B, 2=128B
	    if(afu_tlx_resp_data_read_req(&afu_event, cmd_rd_req, cmd_rd_cnt) != TLX_SUCCESS) {
		error_msg("AFU: FAILED tlx_afu_read_resp");
	    }
	    else {
		debug_msg("AFU: set mem_state = WAITING_FOR_DATA");
		mem_state = WAITING_FOR_DATA;
	    }

	    break;
	case TLX_RSP_UGRADE_RESP:
	    break;
	case TLX_RSP_READ_FAILED:
	    break;
	case TLX_RSP_CL_RD_RESP:
	    break;
	case TLX_RSP_WRITE_RESP:
	    printf("AFU: received response write\n");
	    printf("memory = 0x");
	    for(i=0; i<64; i++) {
		//memory[i]=i;
	 	printf("%02x", (uint8_t)memory[i]);
	    }
	    printf("\n");
	    cdata_bad = 0;
	    afu_tlx_send_cmd_data(&afu_event, cdata_bad, memory);
	    write_resp_completed = 1;
	    break;
	case TLX_RSP_WRITE_FAILED:
	case TLX_RSP_MEM_FLUSH_DONE:
	case TLX_RSP_INTRP_RESP:
	case TLX_RSP_READ_RESP_OW:
	case TLX_RSP_READ_RESP_XW:
	case TLX_RSP_WAKE_HOST_RESP:
	case TLX_RSP_CL_RD_RESP_OW:
	default:
	    break;
    }
}

void
AFU::tlx_afu_config_read()
{
    uint32_t vsec_offset, vsec_data;
    uint16_t bdf;
    uint8_t resp_dl, resp_pl, resp_opcode, rdata_bad;
    uint16_t resp_capptag, byte_cnt;
    uint8_t  resp_code, resp_dp, data_size;
    uint8_t rdata_valid, byte_offset;
    uint64_t  cmd_pa;
    int rc;

    info_msg("AFU:tlx_afu_config_read");
    resp_opcode = 0x01;
    resp_dl = 0x01;	// length 64 byte
    byte_cnt = 4;		
    resp_capptag = afu_event.tlx_afu_cmd_capptag;
    resp_pl = afu_event.tlx_afu_cmd_pl;
    vsec_offset = 0x0000FFFC & afu_event.tlx_afu_cmd_pa;
    byte_offset = 0x0000003F & afu_event.tlx_afu_cmd_pa;
    vsec_data  = descriptor.get_vsec_reg(vsec_offset);	// get vsec data
    debug_msg("AFU: vsec data = 0x%x vsec offset = 0x%x byte offset = 0x%x",
	vsec_data, vsec_offset, byte_offset);
    if(resp_pl == 0x00) {
	data_size = 1;
	switch(vsec_offset) {
	    case 0:
		vsec_data = 0x000000FF & vsec_data;
		break;
	    case 1:
		vsec_data = 0x0000FF00 & vsec_data;
		vsec_data = vsec_data >> 8;
		break;
	    case 2:
		vsec_data = 0x00FF0000 & vsec_data;
		vsec_data = vsec_data >> 16;
		break;
	    case 3:
		vsec_data = 0xFF000000 & vsec_data;
		vsec_data = vsec_data >> 24;
		break;
	    default:
		error_msg("Configuration read offset is not supported 0x%x", vsec_offset);
		break;
	}
    }
    else if(resp_pl == 0x01) {
	data_size = 2;
	switch(vsec_offset) {
	    case 0:
		vsec_data = vsec_data & 0x0000FFFF;
		break;
	    case 2:
		vsec_data = vsec_data & 0xFFFF0000;
		vsec_data = vsec_data >> 16;
		break;
	    default:
		error_msg("Configuration read offset is not supported 0x%x", vsec_offset);
		break;
	}
    }
    else if(resp_pl == 0x02) {
	data_size = 4;
    }

    bdf = (0xFFFF0000 & afu_event.tlx_afu_cmd_pa) >> 16;
    afu_event.afu_tlx_cmd_bdf = bdf;
    info_msg("AFU: BDF = 0x%x", bdf);
    info_msg("AFU: cmd_capptag = 0x%x", resp_capptag);
    memcpy(&afu_event.afu_tlx_rdata_bus, &vsec_data, data_size); 
    byte_shift(afu_event.afu_tlx_rdata_bus, data_size, byte_offset, RIGHT);  
    printf("rdata_bus = 0x");
    for(uint8_t i=0; i<64; i++)
	printf("%02x", afu_event.afu_tlx_rdata_bus[i]);
    printf("\n");
    info_msg("AFU: vsec_offset = 0x%x vsec_data = 0x%x", vsec_offset, vsec_data);
    printf("calling ocse\n");
    rc = afu_cfg_send_resp_and_data(&afu_event, resp_opcode, resp_dl, resp_capptag,
	resp_dp, resp_code, byte_cnt, rdata_valid, afu_event.afu_tlx_rdata_bus, rdata_bad);
    printf("config read rc = 0x%x\n", rc);
    if(rc != TLX_SUCCESS) {

		error_msg("AFU: Failed afu_tlx_send_resp_and_data");
    	}
}

void
AFU::tlx_afu_config_write()
{
    uint8_t  afu_tlx_cmd_rd_req;
    uint8_t  afu_tlx_cmd_rd_cnt;
    uint8_t  afu_resp_opcode;
    uint8_t  resp_dl = 0;
    uint16_t resp_capptag;
    uint8_t  resp_dp = 0;
    uint8_t  resp_code = 0;
    uint8_t  cmd_data_bdi;
    uint8_t  byte_offset, data_size;
    uint32_t config_data, port_data, port_offset, vsec_offset;
    uint32_t cmd_pa;
    
    debug_msg("AFU::tlx_afu_config_write");
    resp_capptag = afu_event.tlx_afu_cmd_capptag;
    cmd_pa = afu_event.tlx_afu_cmd_pa & 0x0000FFFC;   

    debug_msg("AFU: cmd_pa = 0x%x", cmd_pa);

    if(config_state == IDLE) {
	afu_tlx_cmd_rd_req = 0x1;
	afu_tlx_cmd_rd_cnt = 0x1;

	if( afu_tlx_cmd_data_read_req(&afu_event, afu_tlx_cmd_rd_req, afu_tlx_cmd_rd_cnt) !=
	    	TLX_SUCCESS) {
	    printf("AFU: Failed afu_tlx_cmd_data_read_req\n");
	}
	
    	config_state = READY;
	debug_msg("AFU: Set config_state = READY");
    }
    else if(config_state == READY) {
	data_size = 4;
	byte_offset = 0x0000003F & afu_event.tlx_afu_cmd_pa;

    	if(tlx_afu_read_cmd_data(&afu_event, &cmd_data_bdi, afu_event.tlx_afu_cmd_data_bus) !=
		TLX_SUCCESS) {
	   printf("AFU: Failed tlx_afu_read_cmd_data\n");
	}
	printf("cdata_bus = 0x");
	for(uint8_t i=0; i<64; i++)
	    printf("%02x", afu_event.tlx_afu_cmd_data_bus[i]);
	printf("\n");
   	byte_shift(afu_event.tlx_afu_cmd_data_bus, data_size, byte_offset, LEFT); 
	memcpy(&config_data, afu_event.tlx_afu_cmd_data_bus, data_size);
	debug_msg("AFU:config_write: config_data (afu_desc offset) = 0x%x", config_data);
 	debug_msg("AFU:config_write: cmd_pa = 0x%x", cmd_pa);
	debug_msg("AFU:config_write: byte_offset = 0x%x", byte_offset);
	// set afu_enable_reset to check for enable or reset bit
	if(config_data = 0x50c) {
	    afu_enable_reset = 1;
	}
   	if(cmd_pa == 0x40c) {		// config write port
	    port_offset = config_data;	// get afu descriptor offset
	    if(port_offset < 0x0FFF) {
		port_data = descriptor.get_afu_desc_reg(port_offset);	// get afu desc data
		descriptor.set_afu_desc_reg(0x410, port_data);		// write afu desc data to read port
	    }
	    else {
	    	port_data = descriptor.get_port_reg(port_offset);	// get vsec data
	    	descriptor.set_vsec_reg(0x410, port_data);		// write data to read port
	    }
	    port_offset = port_offset | 0x80000000;		// set bit 31 to write port 0x40c
	    descriptor.set_vsec_reg(0x40c, port_offset);
	    debug_msg("AFU: read port 0x410 = 0x%x", descriptor.get_vsec_reg(0x410));
	    debug_msg("AFU: write port 0x40c = 0x%x", descriptor.get_vsec_reg(0x40c));
	}
	else {	// vsec config write 
	    vsec_offset = cmd_pa & 0x00000FFC;
	    descriptor.set_vsec_reg(vsec_offset, config_data);	    
	}
	afu_resp_opcode = 0x04;		// mem write resp
	resp_code = 0x0;
	if(TagManager::request_tlx_credit(RESP_CREDIT)) {
            if(afu_tlx_send_resp(&afu_event, afu_resp_opcode, resp_dl, resp_capptag,
			resp_dp, resp_code) != TLX_SUCCESS) {
	    	printf("AFU: Failed afu_tlx_send_resp\n");
	    }
	}
	else {
		error_msg("AFU: no resp credit available");
	}
//	if(afu_tlx_send_resp(&afu_event, afu_resp_opcode, resp_dl, resp_capptag,
//		resp_dp, resp_code) != TLX_SUCCESS) {
//	    printf("AFU: Failed afu_tlx_send_resp\n");
//	}
	    
	config_state = IDLE;
	printf("\nafu_tlx_cdata_bus\n");
    }
}

void
AFU::tlx_pr_rd_mem()
{
    uint8_t afu_tlx_resp_opcode;
    uint8_t afu_tlx_resp_dl;
    uint8_t afu_tlx_resp_code;
    uint8_t afu_tlx_rdata_valid;
    uint8_t data_size;
    uint16_t afu_tlx_resp_capptag;
    uint32_t mem_offset;
    uint64_t mem_data;

    afu_tlx_resp_opcode = 0x01;		// mem rd response
    afu_tlx_resp_dl = 0x01;		// length 64 byte
    afu_tlx_resp_code = 0x0;
    afu_tlx_rdata_valid = 0x0;
    afu_tlx_resp_capptag = afu_event.tlx_afu_cmd_capptag;

    mem_offset = afu_event.tlx_afu_cmd_pa;
    if(afu_event.tlx_afu_cmd_pl == 3)
	data_size = 8;
    else if(afu_event.tlx_afu_cmd_pl == 2)
	data_size = 4;

    debug_msg("AFU:tlx_pr_rd_mem");
    
    // mmio read data
    descriptor.get_mmio_mem(mem_offset, (char*)&mem_data, data_size);
    debug_msg("mem_offset = 0x%x mem_data = 0x%016llx", mem_offset, mem_data);
    memcpy(&afu_event.afu_tlx_rdata_bus, &mem_data, data_size);
    byte_shift(afu_event.afu_tlx_rdata_bus, data_size, mem_offset, RIGHT);
//    if(TagManager::request_tlx_credit(RESP_DATA_CREDIT)) { 
      if(TagManager::request_tlx_credit(RESP_CREDIT)) {
        if(afu_tlx_send_resp_and_data(&afu_event, afu_tlx_resp_opcode, afu_tlx_resp_dl, 
		afu_tlx_resp_capptag, afu_event.afu_tlx_resp_dp, 
		afu_tlx_resp_code, afu_tlx_rdata_valid, 
		afu_event.afu_tlx_rdata_bus, afu_event.afu_tlx_rdata_bad) != TLX_SUCCESS) {

		error_msg("AFU: Failed afu_tlx_send_resp_and_data");
    	}
    }
    else {
	error_msg("AFU: No response credit available");
    }
}

void
AFU::tlx_pr_wr_mem()
{
    uint8_t  afu_tlx_cmd_rd_req;
    uint8_t  afu_tlx_cmd_rd_cnt;
    uint8_t  cmd_data_bdi;
    uint32_t cmd_pa;
    uint64_t mem_data;
    uint8_t  afu_resp_opcode;
    uint8_t  resp_dl = 0;
    uint16_t resp_capptag;
    uint8_t  resp_dp = 0;
    uint8_t  resp_code = 0;
    uint8_t  byte_offset;
    uint8_t  data_size;		

    debug_msg("AFU:tlx_pr_wr_mem");
    cmd_pa = afu_event.tlx_afu_cmd_pa & 0x0000FFFC;
    resp_capptag = afu_event.tlx_afu_cmd_capptag;
    byte_offset = 0x0000003F & afu_event.tlx_afu_cmd_pa;

    if(mem_state == IDLE) {
	afu_tlx_cmd_rd_req = 0x1;
	afu_tlx_cmd_rd_cnt = 0x1;
//    	if(TagManager::request_tlx_credit(RESP_DATA_CREDIT)) {
//	if(TagManager::request_tlx_credit(RESP_CREDIT)) {
	    if(afu_tlx_cmd_data_read_req(&afu_event, afu_tlx_cmd_rd_req, afu_tlx_cmd_rd_cnt) !=
	        TLX_SUCCESS) {
	    	printf("AFU: Failed afu_tlx_resp_data_read_req\n");
	    }
//	}
//	else {
//	    	error_msg("AFU:tlx_pr_wr_mem: no cmd data credit available");
//	}
	debug_msg("AFU: set mem_state = READY");
	mem_state = READY;
    }
    else if(mem_state == READY) {
	//if(TagManager::request_tlx_credit(RESP_DATA_CREDIT)) {
	    if(tlx_afu_read_cmd_data(&afu_event, &cmd_data_bdi, afu_event.tlx_afu_cmd_data_bus) !=
		TLX_SUCCESS) {
		printf("AFU: Failed tlx_afu_read_cmd_data\n");
	    }
	//}
	//else {
	//    error_msg("AFU: Failed no RESP_DATA_CREDIT available");
	//}
	if(afu_event.tlx_afu_cmd_pl == 3)
	    data_size = 8;
	else if(afu_event.tlx_afu_cmd_pl == 2)
	    data_size = 4;
	printf("mem_data = 0x");
	for(int i=0; i<64; i++)
	    printf("%02x", afu_event.tlx_afu_cmd_data_bus[i]);
	printf("\n");
	byte_shift(afu_event.tlx_afu_cmd_data_bus, data_size, byte_offset, LEFT);
	memcpy(&mem_data, afu_event.tlx_afu_cmd_data_bus, data_size);
	debug_msg("mem_data offset = 0x%x mem_data = 0x%016llx", cmd_pa, mem_data);
	// mmio write
	descriptor.set_mmio_mem(cmd_pa, (char*)&mem_data, data_size);
	//descriptor.set_port_reg(cmd_pa, mem_data);
	afu_resp_opcode = 0x04;		// mem write resp
	resp_code = 0x0;
	if(TagManager::request_tlx_credit(RESP_CREDIT)) {
            if(afu_tlx_send_resp(&afu_event, afu_resp_opcode, resp_dl, resp_capptag,
			resp_dp, resp_code) != TLX_SUCCESS) {
	    	printf("AFU: Failed afu_tlx_send_resp\n");
	    }
	}
	else {
		error_msg("AFU: no resp credit available");
	}
//	if(afu_tlx_send_resp(&afu_event, afu_resp_opcode, resp_dl, resp_capptag,
//		resp_dp, resp_code) != TLX_SUCCESS) {
//	    printf("AFU: Failed afu_tlx_send_resp\n");
//	}

	debug_msg("set mem_state = IDLE");
	mem_state = IDLE;
    }
}


void
AFU::byte_shift(unsigned char *array, uint8_t size, uint8_t offset, uint8_t direction)
{
    uint8_t i;
    switch(direction) {
    case LEFT: 
    	for(i=0; i<size; i++)
    	{
	    array[i] = array[offset+i];
    	}
	break;
    case RIGHT:
	for(i=0; i<size; i++)
	{
	    array[offset+i] = array[i];
	}
	break;
    default:
	break;
    }
}

void 
AFU::write_app_status(uint8_t *address, uint32_t data)
{
    uint8_t ea_addr[9];
    uint32_t cmd_afutag;

    TagManager::request_tag(&cmd_afutag);

    memcpy((void*)&ea_addr, (void*)&address, sizeof(uint64_t));
    printf("AFU: status address = 0x%p and data = 0x%x\n", address, data);
    printf("AFU: afutag = 0x%x\n", cmd_afutag);
    //cmd_op=0x20, dl=0x01, pl=0x02
    memcpy(afu_event.afu_tlx_cdata_bus, &data, 64);

    afu_tlx_send_cmd_and_data(&afu_event, 0x20, afu_event.afu_tlx_cmd_actag, 
	afu_event.afu_tlx_cmd_stream_id, ea_addr, 
	(uint16_t)cmd_afutag, 0x01, 0x03,
#ifdef	TLX4
	afu_event.afu_tlx_cmd_os,
#endif
	afu_event.afu_tlx_cmd_be, afu_event.afu_tlx_cmd_flag,
	afu_event.afu_tlx_cmd_endian, afu_event.afu_tlx_cmd_bdf,
	afu_event.afu_tlx_cmd_pasid, afu_event.afu_tlx_cmd_pg_size, 
	afu_event.afu_tlx_cdata_bus, afu_event.afu_tlx_cdata_bad);

}

void
AFU::read_app_status(uint8_t *address)
{
    uint8_t ea_addr[9];
    uint32_t cmd_afutag;

    TagManager::request_tag(&cmd_afutag);

    memcpy((void*)&ea_addr, (void*)&address, sizeof(uint64_t));
    printf("AFU: status address = 0x%p\n", address);
    printf("AFU: afutag = 0x%x\n", cmd_afutag);
    status_resp_valid = 1;
    // cmd_opcode=0x12, dl=00, pl=03
    afu_tlx_send_cmd(&afu_event, 0x12, afu_event.afu_tlx_cmd_actag,
	afu_event.afu_tlx_cmd_stream_id, ea_addr,
	(uint16_t)cmd_afutag, 0x00, 0x03,
#ifdef	TLX4
	afu_event.afu_tlx_cmd_os,
#endif
	afu_event.afu_tlx_cmd_be, afu_event.afu_tlx_cmd_flag,
	afu_event.afu_tlx_cmd_endian, afu_event.afu_tlx_cmd_bdf,
	afu_event.afu_tlx_cmd_pasid, afu_event.afu_tlx_cmd_pg_size);

    return;
}

void
AFU::resolve_control_event ()
{

        
        for (std::map < uint16_t, MachineController * >::iterator it =
                    context_to_mc.begin (); it != context_to_mc.end (); ++it)
            it->second->disable_all_machines ();
        state = RESET;
	debug_msg("AFU: state = RESET");
        reset_delay = 1000;
}


void
AFU::resolve_response_event (uint32_t cycle)
{
    //if (!TagManager::is_in_use (afu_event.response_tag))
    //    error_msg ("AFU: received tag not in use");


    for (std::map < uint16_t, MachineController * >::iterator it =
                context_to_mc.begin (); it != context_to_mc.end (); ++it) {
        //if (it->second->has_tag (afu_event.response_tag)) {
            it->second->process_response (&afu_event, cycle);
         //   break;
        //}
    }
}

void
AFU::set_seed ()
{
    srand (time (NULL));
}

void
AFU::set_seed (uint32_t seed)
{
    srand (seed);
}

bool 
AFU::get_mmio_read_parity ()
{
    return (global_configs[2] & 0x8000000000000000LL) == 0x8000000000000000;
}
