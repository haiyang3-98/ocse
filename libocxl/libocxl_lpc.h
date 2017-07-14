/*
 * Copyright 2014,2015 International Business Machines
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

#ifndef _LIBOCXL_LPC_H
#define _LIBOCXL_LPC_H

//#include <linux/types.h>
#include <misc/ocxl.h>
#include <libocxl.h>
#include <stdbool.h>
#include <stddef.h>  // for size_t
#include <stdint.h>
#include <stdio.h>  // for off_t

#ifdef __cplusplus
extern "C" {
#endif

// think about an lpc or "host agent memory" set of helper functions
// maybe a map function
// read functions
// write functions
// and an unmap

/*
 * AFU LPC functions
 *
 * The below assessors will ...
 * note - the application is responsible for managing the space held by the lpc
 * there are no malloc or free operations
 */

int ocxl_lpc_map(struct ocxl_afu_h *afu, uint32_t flags);
int ocxl_lpc_unmap(struct ocxl_afu_h *afu);

// write the "size" bytes starting at "data" to the location starting at "offset" in lpc memory known to "afu"
int ocxl_lpc_write(struct ocxl_afu_h *afu, uint64_t offset, uint8_t *data, uint64_t size );

// read the "size" bytes starting at "offset" in lpc memory known to "afu" and save them starting at "data"
int ocxl_lpc_read(struct ocxl_afu_h *afu, uint64_t offset, uint8_t *data, uint64_t size );


#ifdef __cplusplus
}
#endif

#endif
