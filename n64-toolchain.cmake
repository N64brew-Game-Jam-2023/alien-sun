set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mips)

set(CMAKE_AR /opt/libdragon/bin/mips64-elf-ar)
set(CMAKE_ASM_COMPILER /opt/libdragon/bin/mips64-elf-ar)
set(CMAKE_C_COMPILER /opt/libdragon/bin/mips64-elf-gcc)
set(CMAKE_CXX_COMPILER /opt/libdragon/bin/mips64-elf-g++)
set(CMAKE_LINKER /opt/libdragon/bin/mips64-elf-ld)
set(CMAKE_OBJCOPY /opt/libdragon/bin/mips64-elf-objcopy)
set(CMAKE_RANLIB /opt/libdragon/bin/mips64-elf-ranlib)
set(CMAKE_SIZE /opt/libdragon/bin/mips64-elf-size)
set(CMAKE_STRIP /opt/libdragon/bin/mips64-elf-strip)

set(CMAKE_C_FLAGS "-march=vr4300 -mtune=vr4300 -falign-functions=32 \
  -ffunction-sections -fdata-sections -ffast-math -ftrapping-math \
  -fno-associative-math")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE   STATIC_LIBRARY)
