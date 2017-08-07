#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <inttypes.h>
#include "TestAFU_config.h"
#include "tlx_interface_t.h"
//#include "../../libocxl/libocxl.h"
#include "../../libocxl/libocxl_lpc.h"

#define CACHELINE 128
#define MDEVICE "/dev/cxl/afu0.0s"

static int verbose;
static unsigned int buffer_cl = 64;
static unsigned int timeout   = 1;

static void print_help(char *name)
{
    printf("\nUsage:  %s [OPTIONS]\n", name);
    printf("\t--cachelines\tCachelines to copy.  Default=%d\n", buffer_cl);
    printf("\t--timeout   \tDefault=%d seconds\n", timeout);
    printf("\t--verbose   \tVerbose output\n");
    printf("\t--help      \tPrint Usage\n");
    printf("\n");
}

int main(int argc, char *argv[])
{
    int opt, option_index, i;
    int rc, timeout;
    uint8_t *rcacheline, *wcacheline;
    char *status;
    struct ocxl_afu_h *mafu_h;
    MachineConfig machine_config;
    MachineConfigParam config_param;

    static struct option long_options[] = {
	{"cachelines", required_argument, 0	  , 'c'},
	{"timeout",    required_argument, 0	  , 't'},
	{"verbose",    no_argument      , &verbose,   1},
	{"help",       no_argument      , 0	  , 'h'},
	{NULL, 0, 0, 0}
    };

    while((opt = getopt_long(argc, argv, "vhc:t:", long_options, &option_index)) >= 0 )
    {
	switch(opt)
	{
	    case 'v':
	  	break;
	    case 'c':
		buffer_cl = strtoul(optarg, NULL, 0);
		break;
	    case 't':
		timeout = strtoul(optarg, NULL, 0);
		break;
	    case 'h':
		print_help(argv[0]);
		return 0;
	    default:
		print_help(argv[0]);
		return 0;
	}
    }

    // initialize machine
    init_machine(&machine_config);

    // align and randomize cacheline
    if (posix_memalign((void**)&rcacheline, CACHELINE, CACHELINE) != 0) {
	perror("FAILED: posix_memalign for rcacheline");
	goto done;
    }
    if (posix_memalign((void**)&wcacheline, CACHELINE, CACHELINE) != 0) {
	perror("FAILED: posix_memalign for wcacheline");
	goto done;
    }
    if(posix_memalign((void**)&status, 128, 128) != 0) {
	perror("FAILED: posix_memalign for status");
	goto done;
    }

    printf("wcacheline = 0x");
    for(i=0; i<CACHELINE; i++) {
	wcacheline[i] = rand();
	rcacheline[i] = 0x0;
	status[i] = 0x0;
	printf("%02x", (uint8_t)wcacheline[i]);
    }
    printf("\n");
    
    //status[0]=0xff;
    // open master device
    printf("Calling ocxl_afu_open_dev\n");
    
    mafu_h = ocxl_afu_open_dev(MDEVICE);
    if(!mafu_h) {
	perror("cxl_afu_open_dev: "MDEVICE);
	return -1;
    }
    
    // attach device
    printf("Attaching device ...\n");
    rc = ocxl_afu_attach(mafu_h, 0);
    if(rc != 0) {
	perror("cxl_afu_attach:"MDEVICE);
	return rc;
    }

    // mapping device
    printf("Attempt mmio mapping afu registers\n");
    if (ocxl_mmio_map(mafu_h, OCXL_MMIO_LITTLE_ENDIAN) != 0) {
	printf("FAILED: ocxl_mmio_map\n");
	goto done;
    }
    if(ocxl_global_mmio_map(mafu_h, OCXL_MMIO_LITTLE_ENDIAN) != 0) {
	printf("FAILED: ocxl_global_mmio_map\n");
	goto done;
    }
    printf("Attempt lpc memory mapping\n");
    if(ocxl_lpc_map(mafu_h, OCXL_MMIO_LITTLE_ENDIAN) != 0) {
	printf("FAILED: ocxl_lpc_map\n");
	goto done;
    }

    // lpc write
    printf("Attempting lpc write\n");
    ocxl_lpc_write(mafu_h, (uint64_t)rcacheline, wcacheline, 64);

    // lpc read
    printf("Attempting lpc read\n");
    ocxl_lpc_read(mafu_h, (uint64_t)rcacheline, rcacheline, 64);
    printf("rcacheline = 0x");
    for(i=0; i<CACHELINE; i++)
	printf("%02x", (uint8_t)rcacheline[i]);
done:
    // free device
    printf("Freeing device ... \n");
    ocxl_afu_free(mafu_h);

    return 0;
}
