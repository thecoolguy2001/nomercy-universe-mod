CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic -Werror

ENGINE   = engine/universe.c host/demo_roster.c
HEADERS  = engine/universe.h host/demo_roster.h

# ---- N64 build (clang + lld, no external SDK) ----
ROM_IN     = original/nomercy_us_v10.z64
ROM_OUT    = build/nomercy_universe.z64
INSTALL_DIR = /sdcard/Download/NoMercyMods
N64_CC     = clang
N64_FLAGS  = --target=mips-unknown-elf -march=mips2 -mabi=32 -mno-abicalls -fno-pic \
             -G0 -msoft-float -mno-check-zero-division -ffreestanding -fno-builtin \
             -fno-stack-protector -std=c99 -Os -Wall -Wextra -Werror
N64_OBJS   = build/n64/crt0.o build/n64/loader.o build/n64/ui.o build/n64/hw.o \
             build/n64/libc.o build/n64/universe.o build/n64/demo_roster.o
RES_OBJS   = build/n64/res_entry.o build/n64/res_resident.o build/n64/res_libc.o
PAYLOAD_CART = 0x11FB0000

all: build/universe build/test_universe

build/universe: host/cli.c $(ENGINE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ host/cli.c $(ENGINE)

build/test_universe: tests/test_universe.c $(ENGINE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/test_universe.c $(ENGINE)

test: build/test_universe
	./build/test_universe

# ---- ROM ----
rom: $(ROM_OUT)

n64/font.h: tools/gen_font.py
	python3 -I tools/gen_font.py > $@

build/n64/orig_entry.bin: $(ROM_IN) tools/build_rom.py
	@mkdir -p build/n64
	python3 -I tools/build_rom.py extract-entry $(ROM_IN) $@

build/n64/crt0.o: n64/crt0.S build/n64/orig_entry.bin build/n64/resident.bin
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/%.o: n64/%.c n64/hw.h n64/font.h $(HEADERS)
	@mkdir -p build/n64
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/%.o: engine/%.c $(HEADERS)
	@mkdir -p build/n64
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/%.o: host/%.c $(HEADERS)
	@mkdir -p build/n64
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/universe_label.tex: tools/make_label.py tools/nm_assets.py $(ROM_IN)
	@mkdir -p build/n64
	python3 -I tools/make_label.py $(ROM_IN) "Universe Mode" $@

build/n64/universe_label.bin build/n64/universe_label.h: build/n64/universe_label.tex tools/pack_label.py
	python3 -I tools/pack_label.py $< build/n64/universe_label.bin build/n64/universe_label.h 0x1FA8000

build/n64/res_entry.o: n64/resident/entry.S build/n64/universe_label.h
	@mkdir -p build/n64
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/res_%.o: n64/resident/%.c n64/resident/resident.h
	@mkdir -p build/n64
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/res_libc.o: n64/libc.c
	@mkdir -p build/n64
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/resident.elf: $(RES_OBJS) n64/resident/link.ld
	ld.lld -T n64/resident/link.ld -o $@ $(RES_OBJS)

build/n64/resident.bin: build/n64/resident.elf
	llvm-objcopy -O binary $< $@

build/n64/loader.o: n64/loader.c n64/hw.h n64/resident/resident.h
	@mkdir -p build/n64
	$(N64_CC) $(N64_FLAGS) -c -o $@ $<

build/n64/payload.elf: $(N64_OBJS) n64/link.ld
	ld.lld -T n64/link.ld -o $@ $(N64_OBJS)

build/n64/payload.bin: build/n64/payload.elf
	llvm-objcopy -O binary $< $@

build/n64/stub.bin: n64/stub.S build/n64/payload.bin
	$(N64_CC) $(N64_FLAGS) -DPAYLOAD_CART=$(PAYLOAD_CART) \
	    -DPAYLOAD_LEN=$$(( ($$(stat -c %s build/n64/payload.bin) + 7) / 8 * 8 )) \
	    -c -o build/n64/stub.o $<
	llvm-objcopy -O binary -j .text build/n64/stub.o $@

$(ROM_OUT): $(ROM_IN) build/n64/stub.bin build/n64/payload.bin build/n64/universe_label.bin tools/build_rom.py
	python3 -I tools/build_rom.py patch $(ROM_IN) build/n64/stub.bin build/n64/payload.bin $@ build/n64/universe_label.bin

install: $(ROM_OUT)
	mkdir -p $(INSTALL_DIR)
	cp $(ROM_OUT) "$(INSTALL_DIR)/WWF No Mercy - Universe (U).z64"
	@echo "Copied to $(INSTALL_DIR)"

clean:
	rm -rf build/universe build/test_universe build/n64 $(ROM_OUT)

.PHONY: all test rom install clean
