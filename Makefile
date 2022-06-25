#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := M125
SUFFIX := $(shell components/ESP32-RevK/buildsuffix)
MODELS := M125

all:	
	@echo Make: $(PROJECT_NAME)$(SUFFIX).bin
	@idf.py build
	@cp build/$(PROJECT_NAME).bin $(PROJECT_NAME)$(SUFFIX).bin
	@echo Done: $(PROJECT_NAME)$(SUFFIX).bin

set:    wroom solo pico

pico:
	components/ESP32-RevK/setbuildsuffix -S1-PICO-
	@make

wroom:
	components/ESP32-RevK/setbuildsuffix -S1-
	@make

solo:
	components/ESP32-RevK/setbuildsuffix -S1-SOLO
	@make

flash:
	idf.py flash

monitor:
	idf.py monitor

clean:
	idf.py clean

menuconfig:
	idf.py menuconfig

pull:
	git pull
	git submodule update --recursive

update:
	git submodule update --init --recursive --remote
	git commit -a -m "Library update"

# Set GPIO low (whichever CBUS is set to mode 8/GPIO)
bootmode: ftdizap/ftdizap
	./ftdizap/ftdizap --cbus=0

# Flash with GPIO control using CBUS0 (FT230X design)
zap:    bootmode flash
	./ftdizap/ftdizap --cbus=1 --reset

# Program the FTDI
ftdi3: ftdizap/ftdizap
	./ftdizap/ftdizap --serial="RevK" --description="M125" --cbus0-mode=7 --cbus1-mode=13 --cbus2-mode=17 --self-powered=1

ftdi5: ftdizap/ftdizap
	./ftdizap/ftdizap --serial="RevK" --description="M125" --cbus2-mode=17 --self-powered=1

ftdi: ftdizap/ftdizap
	./ftdizap/ftdizap --serial="RevK" --description="M125" --cbus0-mode=7 --cbus1-mode=13

ftdi-invert: ftdizap/ftdizap
	./ftdizap/ftdizap --serial="RevK" --description="M125" --cbus0-mode=7 --cbus1-mode=13 --invert-rts=1 --invert-dtr=1

ftdizap/ftdizap: ftdizap/ftdizap.c
	make -C ftdizap

PCBCase/case: PCBCase/case.c
	make -C PCBCase

scad:	$(patsubst %,KiCad/%.scad,$(MODELS))
stl:	$(patsubst %,KiCad/%.stl,$(MODELS))

%.stl: %.scad
	echo "Making $@"
	/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD $< -o $@
	echo "Made $@"

KiCad/M125.scad: KiCad/M125.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --base=2

KiCad/M1252.scad: KiCad/M1252.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --base=2

KiCad/M1253.scad: KiCad/M1253.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --base=2

KiCad/M1254.scad: KiCad/M1254.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --base=2.6

KiCad/M1255.scad: KiCad/M1255.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --base=2.6

KiCad/LowPower.scad: KiCad/LowPower.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --base=2

