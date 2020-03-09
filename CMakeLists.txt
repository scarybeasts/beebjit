cmake_minimum_required(VERSION 3.4)
project(beebjit)

set_property(SOURCE <myfile>.S PROPERTY LANGUAGE C)

set(BEEBJIT_ASM_FILES asm_x64_common.S asm_x64_inturbo.S asm_x64_jit.S)

add_compile_options(-no-pie)
add_link_options(-no-pie)

foreach (ASM_SRC ${BEEBJIT_ASM_FILES})
    set_property(SOURCE ${ASM_SRC} PROPERTY LANGUAGE C)
endforeach ()

set(BEEBJIT_SRC_FILES main.c bbc.c jit.c defs_6502.c debug.c util.c state.c video.c via.c
        emit_6502.c interp.c inturbo.c state_6502.c sound.c intel_fdc.c timing.c
        jit_compiler.c cpu_driver.c asm_x64_abi.c asm_tables.c
        asm_x64_common.c asm_x64_inturbo.c asm_x64_jit.c
        jit_optimizer.c jit_opcode.c keyboard.c os.c
        teletext.c render.c serial.c log.c test.c disc.c ibm_disc_format.c tape.c
        disc_fsd.c disc_hfe.c disc_ssd.c)


add_executable(beebjit ${BEEBJIT_SRC_FILES} ${BEEBJIT_ASM_FILES})
target_link_libraries(beebjit m X11 Xext pthread asound)
