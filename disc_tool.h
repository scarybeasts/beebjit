#ifndef BEEBJIT_DISC_TOOL_H
#define BEEBJIT_DISC_TOOL_H

#include <stdint.h>

struct disc_tool_struct;

struct disc_drive_struct;

struct disc_tool_struct* disc_tool_create(struct disc_drive_struct* p_drive_0,
                                          struct disc_drive_struct* p_drive_1);
void disc_tool_destroy(struct disc_tool_struct* p_tool);

uint32_t disc_tool_get_pos(struct disc_tool_struct* p_tool);

void disc_tool_set_drive(struct disc_tool_struct* p_tool, uint32_t drive);
void disc_tool_set_is_side_upper(struct disc_tool_struct* p_tool,
                                 int is_side_upper);
void disc_tool_set_track(struct disc_tool_struct* p_tool, uint32_t track);
void disc_tool_set_pos(struct disc_tool_struct* p_tool, uint32_t pos);

void disc_tool_read_fm_data(struct disc_tool_struct* p_tool,
                            uint8_t* p_buf,
                            uint32_t len);

#endif /* BEEBJIT_DISC_TOOL_H */
