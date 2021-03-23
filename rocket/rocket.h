#ifndef BEEBJIT_ROCKET_H
#define BEEBJIT_ROCKET_H

#include <stdint.h>

struct rocket_struct;

struct bbc_struct;

struct rocket_struct* rocket_create(struct bbc_struct* p_bbc,
                                    const char* p_track_list_file_name,
                                    const char* p_prefix,
                                    const char* p_opt_flags);

void rocket_destroy(struct rocket_struct* p_rocket);

void rocket_run(struct rocket_struct* p_rocket);

#endif /* BEEBJIT_ROCKET_H */
