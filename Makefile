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
#   make kbd                         end to end keyboard decode
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

# Generated files are written by tools that can fail partway. Without this, a
# failed recipe leaves an output file newer than its prerequisites, and the next
# make treats it as up to date - so a tools/instrument.py that bailed on a missing
# site would be skipped on the retry and fuzz would run against a partially
# instrumented parser, reporting fewer out-of-bounds accesses than really happen.
.DELETE_ON_ERROR:

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

BINS := $(OUT)/dump $(OUT)/mousetest $(OUT)/kbdtest $(OUT)/fuzz $(OUT)/exhaust \
        $(OUT)/timing $(OUT)/truncate

.PHONY: all dump compare mouse kbd fuzz exhaust timing truncate clean check-target \
        check-ref check-constants

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

# no lifting here: extract_kbd_data and its helpers are all in hid_report.c,
# which $(CORE) already carries
$(OUT)/kbdtest: src/kbdtest.c descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(ASAN) $(INCS) -o $@ src/kbdtest.c $(CORE)

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

kbd: $(OUT)/kbdtest
	@$(OUT)/kbdtest

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
#
# The tree is keyed by resolved commit, not by the name REF was spelled with. A
# branch name is a moving target: keyed by name, the archive is extracted once and
# then never again, because the rule has no prerequisite that ever changes. Every
# later `compare REF=main` would diff against whatever main pointed at the first
# time anyone ran it, silently. Keyed by SHA, a moved branch is simply a path that
# does not exist yet.
REF_SHA := $(shell git -C $(DESKHOP) rev-parse --short $(REF) 2>/dev/null)

REF_TREE := $(B)/tree/$(REF_SHA)
REF_OUT  := $(B)/ref-$(REF_SHA)

check-ref:
	@test -n "$(REF_SHA)" || { \
	  echo "cannot resolve REF=$(REF) in $(DESKHOP)."; \
	  echo "use a branch, tag or commit that exists there, e.g. make compare REF=main"; \
	  exit 1; }

$(REF_TREE)/.stamp:
	@mkdir -p $(REF_TREE)
	@git -C $(DESKHOP) archive $(REF_SHA) src | tar -x -C $(REF_TREE)
	@touch $@

compare: check-ref $(REF_TREE)/.stamp $(OUT)/dump
	@$(MAKE) --no-print-directory SRC=$(CURDIR)/$(REF_TREE) TAG=ref-$(REF_SHA) $(REF_OUT)/dump
	@echo
	@echo "working tree ($(DESKHOP)) vs $(REF) ($(REF_SHA))"
	@echo
	@printf '  %-22s %-22s %s\n' DESCRIPTOR "$(REF)" "working tree"
	@printf '  '; printf -- '-%.0s' $$(seq 1 68); echo
	@fail=0; known=0; seen=0; \
	for d in $$($(OUT)/dump); do \
	  seen=$$((seen+1)); \
	  a=$$($(REF_OUT)/dump $$d 2>&1); arc=$$?; \
	  b=$$($(OUT)/dump $$d 2>&1); brc=$$?; \
	  if [ $$arc -ne 0 ]; then astat='CRASH'; else astat='ok'; fi; \
	  if [ $$brc -ne 0 ] && [ $$arc -ne 0 ]; then bstat='CRASH, as in the reference'; known=$$((known+1)); \
	  elif [ $$brc -ne 0 ]; then bstat='CRASH, NEW'; fail=$$((fail+1)); \
	  elif [ $$arc -ne 0 ]; then bstat='ok, crash fixed'; \
	  elif [ "$$a" = "$$b" ]; then bstat='ok, identical parse'; \
	  else bstat='ok, parse differs'; fi; \
	  printf '  %-22s %-22s %s\n' $$d "$$astat" "$$bstat"; \
	done; \
	echo; \
	if [ $$seen -eq 0 ]; then \
	  echo "  compared nothing: $(OUT)/dump listed no descriptors."; \
	  echo "  Refusing to report success - a comparison of zero devices is not a pass."; \
	  exit 1; fi; \
	if [ $$known -ne 0 ]; then \
	  echo "  $$known descriptor(s) crash on both sides - known bad, not a regression"; fi; \
	if [ $$fail -eq 0 ]; then echo "  $$seen compared, no new crashes in the working tree"; \
	else echo "  $$fail NEW CRASH(ES) - regression"; exit 1; fi

# harness.h hand-copies TinyUSB's item tags and usages. Nothing in a normal build
# checks that copy, and a wrong value would not fail to compile - it would shift an
# offset and make every target report a plausible wrong answer. Deliberately not a
# prerequisite of `all`: the harness does not otherwise need the Pico SDK submodule
# to be populated, and requiring it here would break builds that work fine today.
TUSB_HID := $(DESKHOP)/pico-sdk/lib/tinyusb/src/class/hid/hid.h

# one shell, not two: a bare `test || { ...; exit 0; }` on its own recipe line only
# exits that line's shell, and make would go on to run the check anyway
check-constants:
	@if [ ! -f $(TUSB_HID) ]; then \
	  echo "  skipped: no vendored TinyUSB header at"; \
	  echo "    $(TUSB_HID)"; \
	  echo "  populate the pico-sdk submodule in $(DESKHOP) to run this check"; \
	else \
	  python3 tools/check_constants.py include/harness.h $(TUSB_HID) \
	    $(PARSER) $(REPORT); \
	fi

clean:
	rm -rf $(B)
