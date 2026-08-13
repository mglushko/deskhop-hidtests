# HID parser test harness for deskhop.
#
# Compiles the firmware's own hid_parser.c and hid_report.c on the host, against
# whichever checkout DESKHOP points at. hid_parser.h and hid_report.h are copied
# verbatim into the build dir, where their quoted includes of "main.h" and
# "tusb.h" land on the shims in include/ instead of the real Pico SDK. The structs
# under test are therefore always the ones from the branch being tested.
#
#   make dump D=gameball_gesture     parse one descriptor, print the result
#   make compare REF=main            diff the working tree against a commit
#   make mouse                       end to end mouse decode
#   make fuzz N=40000                bounds check over generated descriptors
#   make truncate                    every prefix of every descriptor, under ASan
#   make exhaust                     usage array exhaustion behaviour
#   make timing                      cost per element vs report count
#   make all
#
#   make dump DESKHOP=/tmp/other-worktree
#   make compare REF=v0.7

DESKHOP ?= $(HOME)/deskhop
REF     ?= main
D       ?= gameball_gesture
N       ?= 40000
SEED    ?= 1

CC     := gcc
WARN   := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable
CFLAGS := -O1 -g $(WARN)
ASAN   := -fsanitize=address -fno-omit-frame-pointer

export ASAN_OPTIONS = detect_leaks=0

B := build

# Which checkout to compile against, and where its outputs go. `compare`
# re-invokes make with these overridden to build a reference version too.
#
# TAG must depend on the target, or switching DESKHOP silently reuses binaries
# built against the previous one: make only compares timestamps, and a freshly
# created worktree looks older than the last build.
SRC ?= $(DESKHOP)
TAG ?= $(notdir $(patsubst %/,%,$(DESKHOP)))

OUT := $(B)/$(TAG)
GEN := $(OUT)/gen

# The target's src/include is deliberately NOT on the include path. A quoted
# #include "main.h" searches the including file's own directory first, so leaving
# it there would pull in deskhop's real main.h and the whole Pico SDK with it.
# Instead the two headers we need are copied into $(GEN), where their own quoted
# includes resolve to the shims in include/.
INCS := -I$(GEN) -Iinclude -I.

PARSER := $(SRC)/src/hid_parser.c
REPORT := $(SRC)/src/hid_report.c

# Headers copied verbatim from the target. All of these are standalone (stdint
# only, or each other), so none of them drags in the Pico SDK. packet.h carries
# KBD_REPORT_LENGTH and friends, which hid_report.c uses.
COPY_HDRS := hid_parser.h hid_report.h packet.h protocol.h constants.h
HDRS      := $(addprefix $(GEN)/,$(COPY_HDRS))

# get_keyboard is lifted rather than stubbed: it decides which keyboard_t a
# report ID maps to, so a hand written version would quietly change results.
CORE := $(PARSER) $(REPORT) src/stubs.c $(GEN)/lifted_kbd.c

BINS := $(OUT)/dump $(OUT)/mousetest $(OUT)/fuzz $(OUT)/exhaust $(OUT)/timing \
        $(OUT)/truncate

.PHONY: all dump compare mouse fuzz exhaust timing truncate clean check-target

all: check-target $(BINS)
	@echo "built against $(SRC) -> $(OUT)/"

check-target:
	@test -f $(PARSER) || { \
	  echo "no hid_parser.c under $(SRC)."; \
	  echo "point DESKHOP at a deskhop checkout, e.g. make DESKHOP=~/deskhop"; \
	  exit 1; }

$(GEN):
	@mkdir -p $(GEN)

# ---- headers, copied verbatim from the target --------------------------------

$(GEN)/%.h: $(SRC)/src/include/%.h | $(GEN)
	@cp $< $@

# ---- generated sources -------------------------------------------------------

$(GEN)/lifted_kbd.c: $(SRC)/src/keyboard.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ get_keyboard

$(GEN)/lifted_mouse.c: $(SRC)/src/mouse.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ extract_value extract_report_values

$(GEN)/hid_parser_instr.c: $(PARSER) tools/instrument.py | $(GEN)
	@python3 tools/instrument.py $< $@

# ---- binaries ----------------------------------------------------------------

$(OUT)/dump: src/dump.c descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(ASAN) $(INCS) -o $@ src/dump.c $(CORE)

$(OUT)/mousetest: src/mousetest.c descriptors.h $(HDRS) $(CORE) $(GEN)/lifted_mouse.c | $(GEN)
	$(CC) $(CFLAGS) $(ASAN) $(INCS) -o $@ src/mousetest.c $(GEN)/lifted_mouse.c $(CORE)

$(OUT)/exhaust: src/exhaust.c descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(ASAN) $(INCS) -o $@ src/exhaust.c $(CORE)

$(OUT)/truncate: src/truncate.c descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(ASAN) $(INCS) -o $@ src/truncate.c $(CORE)

# no ASan: this one is a stopwatch, and the fuzzer clamps rather than faults
$(OUT)/timing: src/timing.c $(HDRS) $(CORE) | $(GEN)
	$(CC) -O2 $(WARN) $(INCS) -o $@ src/timing.c $(CORE)

$(OUT)/fuzz: src/fuzz.c $(HDRS) $(GEN)/hid_parser_instr.c $(REPORT) src/stubs.c $(GEN)/lifted_kbd.c | $(GEN)
	$(CC) $(CFLAGS) $(INCS) -o $@ src/fuzz.c $(GEN)/hid_parser_instr.c $(REPORT) \
	    src/stubs.c $(GEN)/lifted_kbd.c

# ---- targets -----------------------------------------------------------------

dump: $(OUT)/dump
	@$(OUT)/dump $(D)

mouse: $(OUT)/mousetest
	@$(OUT)/mousetest

exhaust: $(OUT)/exhaust
	@$(OUT)/exhaust

truncate: $(OUT)/truncate
	@$(OUT)/truncate

timing: $(OUT)/timing
	@$(OUT)/timing

fuzz: $(OUT)/fuzz
	@$(OUT)/fuzz $(N) $(SEED)

# Materialise a reference commit, build the same harness against it, and diff the
# parse of every descriptor. This is the target that proves a parser change is
# inert on known good devices while fixing the broken one.
$(B)/tree/$(REF)/.stamp:
	@mkdir -p $(B)/tree/$(REF)
	@git -C $(DESKHOP) archive $(REF) src | tar -x -C $(B)/tree/$(REF)
	@touch $@

compare: $(B)/tree/$(REF)/.stamp $(OUT)/dump
	@$(MAKE) --no-print-directory SRC=$(CURDIR)/$(B)/tree/$(REF) TAG=ref-$(REF) $(B)/ref-$(REF)/dump
	@echo
	@echo "working tree ($(DESKHOP)) vs $(REF)"
	@echo
	@printf '  %-22s %-22s %s\n' DESCRIPTOR "$(REF)" "working tree"
	@printf '  '; printf -- '-%.0s' $$(seq 1 68); echo
	@fail=0; \
	for d in $$($(OUT)/dump); do \
	  a=$$($(B)/ref-$(REF)/dump $$d 2>&1); arc=$$?; \
	  b=$$($(OUT)/dump $$d 2>&1); brc=$$?; \
	  if [ $$arc -ne 0 ]; then astat='CRASH'; else astat='ok'; fi; \
	  if [ $$brc -ne 0 ]; then bstat='CRASH'; fail=1; \
	  elif [ "$$a" = "$$b" ]; then bstat='ok, identical parse'; \
	  else bstat='ok, parse differs'; fi; \
	  printf '  %-22s %-22s %s\n' $$d "$$astat" "$$bstat"; \
	done; \
	echo; \
	if [ $$fail -eq 0 ]; then echo "  no crashes in the working tree"; \
	else echo "  FAILURES PRESENT"; exit 1; fi

clean:
	rm -rf $(B)
