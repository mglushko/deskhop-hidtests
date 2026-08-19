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
#   make consumer                    end to end consumer and system control
#   make fuzz N=40000                bounds check over generated descriptors
#   make truncate                    every prefix of every descriptor, under ASan
#   make shortreport                 every prefix of every report, under ASan
#   make dispatch                    which receiver does a report actually reach?
#   make exhaust                     usage array exhaustion behaviour
#   make timing                      cost per element vs report count
#   make test                        the regression gate: mouse, kbd, consumer, constants
#   make findings                    the bounds checks, for their numbers
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
WARN   := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
CFLAGS := -O1 -g $(WARN)

# ASan catches reads past a descriptor or a report, which is why both are copied
# into exact-size heap allocations before use. UBSan catches a different class it
# cannot see at all: get_report_value() computes (1u << val->size) and
# 0xFFFFFFFFU << val->size, and val->size is the *swapped* Report Count for 1-bit
# fields (hid_parser.c, handle_main_input), so a mouse declaring 40 one-bit
# buttons shifts by 40. That is undefined, and on x86 it silently takes the shift
# mod 32 and returns a plausible wrong number rather than faulting.
SAN    := -fsanitize=address,undefined -fno-omit-frame-pointer \
          -fno-sanitize-recover=all

export ASAN_OPTIONS = detect_leaks=0
export UBSAN_OPTIONS = print_stacktrace=1

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
        $(OUT)/timing $(OUT)/truncate $(OUT)/shortreport $(OUT)/cctest \
        $(OUT)/dispatchtest

.PHONY: all dump compare mouse kbd consumer fuzz exhaust timing truncate shortreport \
        dispatch clean \
        check-target \
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

# get_or_add_keyboard exists only on a tree that separates parse-time allocation from
# decode-time lookup. Lift it where it is there; where it is not, the target's own
# hid_report.c does not reference it either, so leaving it out is correct rather than
# a gap - and lift.py would fail loudly if this guessed wrong.
KBD_LIFT := get_keyboard $(shell grep -q 'get_or_add_keyboard' $(SRC)/src/keyboard.c 2>/dev/null && echo get_or_add_keyboard)

# Same grep, as a flag for the decode tests. MAX_NKRO_BLOCKS no longer separates the
# trees on its own: #359 defines it and so does every tree built on top, including ones
# without this fix, so a third state is needed for the devices whose answer it moves.
KBD_MULTI := $(shell grep -q 'get_or_add_keyboard' $(SRC)/src/keyboard.c 2>/dev/null && echo -DHARNESS_MULTI_KEYBOARD)

# Does the target bound extract_bit_variable against the report length? On a tree that
# does not, feeding a 6KRO report to a collection wrongly flagged NKRO walks the bitmap
# straight off the end and ASan aborts the whole run. That read is a real finding, and
# truncate and shortreport already measure it across the corpus; kbdtest declines the one
# case that would fault rather than taking the suite down with it.
KBD_BOUNDED := $(shell grep -q 'byte_index >= len' $(SRC)/src/hid_report.c 2>/dev/null && echo -DHARNESS_BOUNDED_BITMAP)

$(GEN)/lifted_kbd.c: $(SRC)/src/keyboard.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ $(KBD_LIFT)

$(GEN)/lifted_mouse.c: $(SRC)/src/mouse.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ extract_value extract_report_values

# The two receivers PR #358 changes. Lifted rather than reimplemented for the same
# reason as everything else here: a hand copy would answer the question "does this
# PR do anything" with whatever the copy happened to say. The senders one level
# below them are NOT lifted - see src/recorders.c for where the cut is and why.
$(GEN)/lifted_cc.c: $(SRC)/src/keyboard.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ process_consumer_report process_system_report

# usb.c's routing is liftable only on a tree that factored it out as pick_receiver().
# Everywhere else src/dispatch.h's model stands in and dispatchtest says so, because a
# model reports what it was written to say rather than what the firmware does. One
# grep, and only to decide how dispatchtest links.
LIFTABLE_DISPATCH := $(shell grep -l 'process_report_f pick_receiver' $(SRC)/src/usb.c 2>/dev/null)

ifneq ($(LIFTABLE_DISPATCH),)
DISPATCH_SRC  := $(GEN)/lifted_dispatch.c
DISPATCH_FLAG := -DHARNESS_LIFT_DISPATCH
else
DISPATCH_SRC  :=
DISPATCH_FLAG :=
endif

$(GEN)/lifted_dispatch.c: $(SRC)/src/usb.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ report_carries_id pick_receiver

$(GEN)/hid_parser_instr.c: $(PARSER) tools/instrument.py | $(GEN)
	@python3 tools/instrument.py $< $@

# ---- binaries ----------------------------------------------------------------

$(OUT)/dump: src/dump.c descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) -o $@ src/dump.c $(CORE)

$(OUT)/mousetest: src/mousetest.c src/cases_mouse.h src/dispatch.h descriptors.h $(HDRS) $(CORE) $(GEN)/lifted_mouse.c | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) -o $@ src/mousetest.c $(GEN)/lifted_mouse.c $(CORE)

# no lifting here: extract_kbd_data and its helpers are all in hid_report.c,
# which $(CORE) already carries
$(OUT)/kbdtest: src/kbdtest.c src/cases_kbd.h descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) $(KBD_MULTI) $(KBD_BOUNDED) -o $@ src/kbdtest.c $(CORE)

$(OUT)/exhaust: src/exhaust.c descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) -o $@ src/exhaust.c $(CORE)

$(OUT)/truncate: src/truncate.c descriptors.h $(HDRS) $(CORE) | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) -o $@ src/truncate.c $(CORE)

# The one target that does NOT link src/stubs.c's consumer and system stubs:
# -DHARNESS_LIFT_CC keeps them out, and $(GEN)/lifted_cc.c supplies the real bodies
# from the branch under test. src/recorders.c supplies what those bodies reach for.
$(OUT)/cctest: src/cctest.c src/cases_cc.h descriptors.h $(HDRS) $(CORE) \
               $(GEN)/lifted_cc.c src/recorders.c | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) -DHARNESS_LIFT_CC -o $@ src/cctest.c \
	    $(GEN)/lifted_cc.c src/recorders.c $(CORE)

# Routing, not decode, so the only thing it needs out of $(CORE) is the four
# distinguishable receiver addresses in src/stubs.c. $(DISPATCH_SRC) is the target's
# own pick_receiver() where the target has one, and empty otherwise.
$(OUT)/dispatchtest: src/dispatchtest.c src/dispatch.h descriptors.h $(HDRS) $(CORE) \
                     $(DISPATCH_SRC) | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) $(DISPATCH_FLAG) -o $@ src/dispatchtest.c \
	    $(DISPATCH_SRC) $(CORE)

# needs lifted_mouse.c: it drives extract_report_values, the same entry point
# mousetest uses, so that the truncated reports go through the firmware's own
# extraction rather than a reimplementation of it
# $(KBD_BOUNDED) as well, so both users of cases_kbd.h see the same device list. Without
# it the 8BitDo would be in one binary and not the other, which is a confusing thing to
# debug later; the coverage it would add here on an unbounded tree is the same overread
# truncate already counts.
$(OUT)/shortreport: src/shortreport.c src/cases_mouse.h src/cases_kbd.h descriptors.h $(HDRS) \
                    $(CORE) $(GEN)/lifted_mouse.c | $(GEN)
	$(CC) $(CFLAGS) $(SAN) $(INCS) $(KBD_BOUNDED) -o $@ src/shortreport.c \
	    $(GEN)/lifted_mouse.c $(CORE)

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

consumer: $(OUT)/cctest
	@$(OUT)/cctest

exhaust: $(OUT)/exhaust
	@$(OUT)/exhaust

shortreport: $(OUT)/shortreport
	@$(OUT)/shortreport

dispatch: $(OUT)/dispatchtest
	@$(OUT)/dispatchtest

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
#
# Only defined when compare is actually being asked for. These names appear in
# rule targets, which make expands while reading the file, so an unconditional
# definition runs `git rev-parse` on every single invocation - `make clean`, `make
# dump`, a tab-completion probe. Nothing else here needs a git repo at $(DESKHOP)
# at all, and the sub-make below is spelled with TAG overridden so it resolves
# through the ordinary $(OUT) rules without needing REF_SHA either.
ifneq ($(filter compare,$(MAKECMDGOALS)),)

REF_SHA  := $(shell git -C $(DESKHOP) rev-parse --short $(REF) 2>/dev/null)
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
	@fail=0; known=0; seen=0; differed=''; \
	for d in $$($(OUT)/dump); do \
	  seen=$$((seen+1)); \
	  a=$$($(REF_OUT)/dump $$d 2>&1); arc=$$?; \
	  b=$$($(OUT)/dump $$d 2>&1); brc=$$?; \
	  if [ $$arc -ne 0 ]; then astat='CRASH'; else astat='ok'; fi; \
	  if [ $$brc -ne 0 ] && [ $$arc -ne 0 ]; then bstat='CRASH, as in the reference'; known=$$((known+1)); \
	  elif [ $$brc -ne 0 ]; then bstat='CRASH, NEW'; fail=$$((fail+1)); \
	  elif [ $$arc -ne 0 ]; then bstat='ok, crash fixed'; \
	  elif [ "$$a" = "$$b" ]; then bstat='ok, identical parse'; \
	  else bstat='ok, parse differs'; differed="$$differed $$d"; fi; \
	  printf '  %-22s %-22s %s\n' $$d "$$astat" "$$bstat"; \
	done; \
	echo; \
	if [ -n "$$differed" ] && [ -n "$(V)" ]; then \
	  for d in $$differed; do \
	    echo "  --- $$d ---"; \
	    $(REF_OUT)/dump $$d >$(B)/.cmp-ref 2>&1; \
	    $(OUT)/dump $$d >$(B)/.cmp-new 2>&1; \
	    diff -u $(B)/.cmp-ref $(B)/.cmp-new | tail -n +3 | sed 's/^/  /'; \
	    echo; \
	  done; \
	  rm -f $(B)/.cmp-ref $(B)/.cmp-new; \
	elif [ -n "$$differed" ]; then \
	  echo "  re-run with V=1 to see what changed in:$$differed"; \
	  echo; \
	fi; \
	if [ $$seen -eq 0 ]; then \
	  echo "  compared nothing: $(OUT)/dump listed no descriptors."; \
	  echo "  Refusing to report success - a comparison of zero devices is not a pass."; \
	  exit 1; fi; \
	if [ $$known -ne 0 ]; then \
	  echo "  $$known descriptor(s) crash on both sides - known bad, not a regression"; fi; \
	if [ $$fail -eq 0 ]; then echo "  $$seen compared, no new crashes in the working tree"; \
	else echo "  $$fail NEW CRASH(ES) - regression"; exit 1; fi

endif  # compare in MAKECMDGOALS

# The regression gate: everything that must pass against *any* firmware worth
# shipping, so a red `make test` means the harness moved or a known good device
# stopped decoding. Safe to wire into CI.
#
# fuzz, truncate, shortreport and dispatch are deliberately NOT here. They fail by design on
# firmware that has the bug they look for - fuzz and truncate both fail on main
# today, and truncate still fails on the #332 fix - so folding them in would make
# this permanently red and worth nothing. Their exit status is the finding, not a
# regression. `make findings` runs those, and reports rather than gates.
#
# check-constants is last: it is the only one needing the Pico SDK submodule
# populated, and it skips cleanly when it is not.
.PHONY: test findings
test: mouse kbd consumer check-constants
	@echo
	@echo "known good decode unchanged against $(SRC)"

# The bounds, overread and routing checks, run for their numbers. Each prints its own
# summary and its own exit status is ignored here on purpose: see above.
findings: $(OUT)/fuzz $(OUT)/truncate $(OUT)/shortreport $(OUT)/dispatchtest
	@echo "=== fuzz ==="
	-@$(OUT)/fuzz $(N) $(SEED)
	@echo; echo "=== truncate ==="
	-@$(OUT)/truncate
	@echo; echo "=== shortreport ==="
	-@$(OUT)/shortreport
	@echo; echo "=== dispatch ==="
	-@$(OUT)/dispatchtest
	@echo
	@echo "these fail when they find something; read the counts, not the status"

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
