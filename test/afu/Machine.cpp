#include "Machine.h"
#include "MachineController.h"

#include <stdlib.h>

MachineController::Machine::Machine (uint16_t c)
{
    reset ();

    config[0] = (config[0] & 0xFFFF0000FFFFFFFFLL) | ((uint64_t) c << 32);
}

void
MachineController::Machine::reset ()
{
    delay = 0;
    command = NULL;

    for (uint32_t i = 0; i < SIZE_CONFIG_TABLE; ++i)
        config[i] = 0;

    for (uint32_t i = 0; i < SIZE_CACHE_LINE; ++i)
        cache_line[i] = 0;
}

void
MachineController::Machine::read_machine_config (AFU_EVENT* afu_event)
{
    context = (config[0] >> 32) & 0xFFFF;

    min_delay = (config[0] >> 16) & 0xFFFF;
    max_delay = config[0] & 0xFFFF;

    if (min_delay > max_delay)
        error_msg
        ("Machine: min_delay is larger than max_delay (min_delay = %d, max_delay = %d)",
         min_delay, max_delay);
    delay =
        (max_delay ==
         min_delay) ? max_delay : rand () % (max_delay - min_delay) +
        min_delay;

    abort = (config[1] >> 60) & 0x7;
    command_size = (config[1] >> 48) & 0xFFF;

    memory_base_address = config[2];
    memory_size = config[3];

    uint16_t command_code = (config[0] >> 48) & 0x1FFF;
    bool command_address_parity = get_command_address_parity ();
    bool command_code_parity = get_command_code_parity ();
    bool command_tag_parity = get_command_tag_parity ();
    bool buffer_read_parity = get_buffer_read_parity ();

    if (command)
        delete command;
    printf("command code = 0x%x\n", command_code);

    switch (command_code) {
    case AFU_CMD_PR_RD_WNITC:
    case AFU_CMD_DMA_PR_W:
	command = 
	    new LoadCommand (command_code, command_address_parity,
			     command_code_parity, command_tag_parity,
			     buffer_read_parity);
	break;
    case AFU_CMD_DMA_W:
	command = new StoreCommand ( command_code, command_address_parity,
		command_code_parity, command_tag_parity, buffer_read_parity);
	break;
    case AFU_CMD_INTRP_REQ:
        command =
            new OtherCommand (command_code, command_address_parity,
                              command_code_parity, command_tag_parity,
                              buffer_read_parity);
        break;
    default:
        error_msg
        ("MachineController::Machine::read_machine_config(): command code 0x%x is currently not supported",
         command_code);
    }

}

void
MachineController::Machine::record_command (bool error_state, uint16_t cycle)
{
    uint16_t data = (error_state) ? 1 << 15 : 0;

    data |= cycle & 0x7FFF;
    config[1] = (config[1] & 0xFFFFFFFFFFFF0000LL) | ((uint64_t) data);
}

void
MachineController::Machine::record_response (bool error_state, uint16_t cycle,
        uint8_t response_code)
{
    uint16_t data = (error_state) ? 1 << 15 : 0;

    data |= cycle & 0x7FFF;
    config[1] = (config[1] & 0xFFFFFFFF0000FFFFLL) | ((uint64_t) data << 16);
    config[1] =
        (config[1] & 0xFFFFFF00FFFFFFFFLL) | ((uint64_t) response_code << 32);
}

void
MachineController::Machine::clear_response ()
{
    config[1] |= 0xFF00000000;
}

uint8_t MachineController::Machine::get_command_address_parity () const
{
    return (uint8_t) ((config[1] & 0x800000000000) >> 47);
}

uint8_t
MachineController::Machine::get_command_code_parity () const
{
    return (uint8_t) ((config[1] & 0x400000000000) >> 46);
}

uint8_t
MachineController::Machine::get_command_tag_parity () const
{
    return (uint8_t) ((config[1] & 0x200000000000) >> 45);
}

uint8_t
MachineController::Machine::get_buffer_read_parity () const
{
    return (uint8_t) ((config[1] & 0x100000000000) >> 44);
}

void
MachineController::Machine::change_machine_config (uint16_t index, uint64_t data)
{
    config[index] = data;
}

uint32_t MachineController::Machine::get_machine_config (uint32_t offset)
{
    if (offset >= SIZE_CONFIG_TABLE * 2)
        error_msg
        ("Machine::change_machine_config config table offset exceeded size of config table");

    if (offset % 2 == 1)
        return (uint32_t) (config[offset / 2] & 0x00000000FFFFFFFFLL);
    else
        return (uint32_t) ((config[offset / 2] & 0xFFFFFFFF00000000LL) >> 32);
}

bool MachineController::Machine::attempt_new_command (AFU_EVENT * afu_event,
        uint32_t tag,
        bool error_state,
        uint16_t cycle)
{

    // only send new command if
    // 1. previous command has completed
    // 2. delay is 0

    if (!is_enabled ())
        error_msg
        ("MachineController::Machine::attempt_new_command(): attemp to send new command when machine is not enabled");

    if ((!command || command->is_completed ()) && delay == 0) {
        debug_msg("Machine::attempt_new_command: read_machine_config");
	read_machine_config (afu_event);
	
        // randomly generates address within the range
        uint64_t
        address_offset =
            (rand () % (memory_size - (command_size - 1))) & ~(command_size -
                    1);
	debug_msg("Machine::attempt_new_command: command->send_command");
        command->send_command (afu_event, tag,
                               memory_base_address,
                               command_size, abort, context);

        record_command (error_state, cycle);
        clear_response ();

        if (is_enabled_once ()) {
            disable_once ();
        }

        return true;
    }

    return false;
}

void
MachineController::Machine::advance_cycle ()
{
    if (is_enabled () && (!command || command->is_completed ()) && delay > 0) {
        --delay;
    }

    if (!is_enabled ())
        delay = 0;
}

void
MachineController::Machine::process_response (AFU_EVENT * afu_event,
        bool error_state,
        uint16_t cycle)
{
}

void
MachineController::Machine::process_buffer_write (AFU_EVENT * afu_event)
{
    command->process_command (afu_event, cache_line);
}

void
MachineController::Machine::process_buffer_read (AFU_EVENT * afu_event)
{
    command->process_command (afu_event, cache_line);
}

void
MachineController::Machine::disable_once ()
{
    config[0] &= ~0x4000000000000000;
}

void
MachineController::Machine::disable ()
{
    config[0] &= ~0xC000000000000000;
    delay = 0;
}

bool
MachineController::Machine::is_enabled () const
{
    bool enable_always = ((config[0] >> 63) == 0x1);
    bool enable_once = (((config[0] >> 62) & 0x1) == 0x1);
  
//    if(enable_always)
//     printf("machine is enabled = 0x%d\n", enable_always);
    return enable_always || enable_once;
}

bool
MachineController::Machine::is_enabled_once () const
{
    return ((config[0] >> 62) & 0x1);
}

bool
MachineController::Machine::is_completed () const
{
    if (!command || command->is_completed ())
        return true;

    return false;

}

bool
MachineController::Machine::is_restart () const
{
    if (!command)
        error_msg
        ("MachineController::Machine: calling command->is_restart() when command is not defined");
    return command->is_restart ();
}

MachineController::Machine::~Machine ()
{
    if (command)
        delete command;
}


