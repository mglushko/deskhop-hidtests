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
#   make compare REF=main V=1        the same, printing each descriptor's diff
#   make mouse                       end to end mouse decode
#   make kbd                         end to end keyboard decode
#   make consumer                    end to end consumer and system control
#   make fuzz N=40000 SEED=1         bounds check over generated descriptors
#   make truncate                    every prefix of every descriptor, under ASan
#   make shortreport                 every prefix of every report, under ASan
#   make dispatch                    which receiver does a report actually reach?
#   make exhaust                     usage array exhaustion behaviour
#   make timing                      cost per element vs report count
#   make check-constants             harness.h against the vendored TinyUSB header
#   make check-parse                 add_descriptor.py's reader against every dump shape
#   make check-cli                   the replay tools' command lines, at full length only
#   make test-sleepwake              the BOOTSEL rig's gesture logic, on the host
#   make test                        the regression gate: mouse, kbd, consumer,
#                                    check-parse, check-cli, check-constants and
#                                    test-sleepwake
#   make findings                    the four that fail by design, for their numbers
#   make corpus                      regenerate the table in CORPUS.md
#   make all                         build everything without running it
#   make clean
#
#   make dump DESKHOP=/tmp/other-worktree
#   make compare REF=v0.7

DESKHOP ?= $(HOME)/deskhop
REF     ?= main
D       ?= gameball_gesture
N       ?= 40000
SEED    ?= 1
# V=1 makes compare print the parse diff of every descriptor that changed.
V       ?=

CC     := gcc
WARN   := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
CFLAGS := -O1 -g $(WARN)

# ASan catches reads past a descriptor or a report, hence both are copied into exact-size
# heap allocations. UBSan catches what ASan cannot: get_report_value() computes
# (1u << val->size) and 0xFFFFFFFFU << val->size, and val->size is the *swapped* Report
# Count for 1-bit fields (hid_parser.c, handle_main_input), so a mouse declaring 40
# one-bit buttons shifts by 40.
# That is undefined; on x86 it silently comes out mod 32 as a plausible wrong number.
SAN    := -fsanitize=address,undefined -fno-omit-frame-pointer \
          -fno-sanitize-recover=all

export ASAN_OPTIONS = detect_leaks=0
export UBSAN_OPTIONS = print_stacktrace=1

# Generated files are written by tools that can fail partway. Without this a failed
# recipe leaves an output newer than its prerequisites, so a tools/instrument.py that
# bailed on a missing site is skipped on the retry and fuzz runs against a partially
# instrumented parser, reporting fewer out-of-bounds accesses than really happen.
.DELETE_ON_ERROR:

B := build

# Which checkout to compile against and where its outputs go; `compare` re-invokes make
# with both overridden to build a reference version. TAG must depend on the target, or
# switching DESKHOP silently reuses binaries built against the previous one: make only
# compares timestamps, and a fresh worktree looks older than the last build. The basename
# alone would let two checkouts called deskhop share a directory, hence the path hash.
SRC ?= $(DESKHOP)
TAG ?= $(notdir $(patsubst %/,%,$(DESKHOP)))-$(shell printf '%s' '$(abspath $(DESKHOP))' | md5sum | cut -c1-7)

OUT := $(B)/$(TAG)
GEN := $(OUT)/gen

# The harness's own headers and this file are prerequisites of every binary. The copied
# target headers are already in $(HDRS); without these, a corrected constant in harness.h
# or a widened probe rebuilds nothing, and make hands back the old binary with a straight
# face.
DEPS := Makefile include/harness.h include/main.h include/tusb.h src/support.h

# The target's src/include is deliberately NOT on the include path: a quoted #include
# "main.h" searches the including file's own directory first and would pull in deskhop's
# real main.h and the whole Pico SDK. The headers needed are copied into $(GEN) instead,
# where their own quoted includes resolve to the shims in include/.
INCS := -I$(GEN) -Iinclude -I.

PARSER := $(SRC)/src/hid_parser.c
REPORT := $(SRC)/src/hid_report.c

# Headers copied verbatim from the target. All of these are standalone (stdint
# only, or each other), so none of them drags in the Pico SDK. packet.h carries
# KBD_REPORT_LENGTH and friends, which hid_report.c uses. usb_descriptors.h is
# macros only and carries REPORT_ID_NONE, which usb.c's report callback names
# since upstream 4d113ac; its descriptor macros expand only where they are used,
# which is nowhere here.
COPY_HDRS := hid_parser.h hid_report.h packet.h protocol.h constants.h usb_descriptors.h
HDRS      := $(addprefix $(GEN)/,$(COPY_HDRS))

# get_keyboard is lifted rather than stubbed: it decides which keyboard_t a
# report ID maps to, so a hand written version would quietly change results.
CORE := $(PARSER) $(REPORT) src/stubs.c $(GEN)/lifted_kbd.c

BINS := $(OUT)/dump $(OUT)/mousetest $(OUT)/kbdtest $(OUT)/fuzz $(OUT)/exhaust \
        $(OUT)/timing $(OUT)/truncate $(OUT)/shortreport $(OUT)/cctest \
        $(OUT)/dispatchtest

.PHONY: corpus all dump compare mouse kbd consumer fuzz exhaust timing truncate shortreport \
        dispatch clean \
        check-target \
        check-constants check-parse check-cli

all: check-target $(BINS)
	@echo "built against $(SRC) -> $(OUT)/"

check-target:
	@test -f $(PARSER) || { \
	  echo "no hid_parser.c under $(SRC)."; \
	  echo "point DESKHOP at a deskhop checkout, e.g. make DESKHOP=~/deskhop"; \
	  exit 1; }
	@for f in $(sort $(PROBED)); do test -f $(SRC)/src/$$f || { \
	  echo "no $$f under $(SRC)/src, so every probe that reads it answered 'absent'."; \
	  echo "The tree is incomplete or the file moved; fix that rather than trust this run."; \
	  exit 1; }; done

$(GEN):
	@mkdir -p $(GEN)

# ---- headers, copied verbatim from the target --------------------------------

$(GEN)/%.h: $(SRC)/src/include/%.h | $(GEN)
	@cp $< $@

# ---- generated sources -------------------------------------------------------

# Every probe below is one grep over one file of the target tree, answered as a -D flag
# for the case tables: `has` is the grep, `probe` the flag. Both read as "absent" when
# the file is missing, so `has` records each file it reads in PROBED and check-target
# insists every one of them exists before anything is built.
PROBED :=
has   = $(eval PROBED += $(1))$(shell grep -qE '$(2)' $(SRC)/src/$(1) 2>/dev/null && echo y)
probe = $(if $(call has,$(1),$(2)),-D$(3))

# get_or_add_keyboard exists only on a tree that separates parse-time allocation from
# decode-time lookup. Lift it where it is there; where it is not, the target's own
# hid_report.c does not reference it either, so leaving it out is correct rather than
# a gap - and lift.py would fail loudly if this guessed wrong.
HAS_MULTI_KBD := $(call has,keyboard.c,get_or_add_keyboard)
KBD_LIFT      := get_keyboard $(if $(HAS_MULTI_KBD),get_or_add_keyboard)

# The same answer, as a flag for the decode tests. MAX_NKRO_BLOCKS no longer separates the
# trees on its own: #359 defines it and so does every tree built on top, including ones
# without this fix, so a third state is needed for the devices whose answer it moves.
KBD_MULTI := $(if $(HAS_MULTI_KBD),-DHARNESS_MULTI_KEYBOARD)

# Does the target bound extract_bit_variable against the report length? Unbounded, a 6KRO
# report to a collection wrongly flagged NKRO walks off the bitmap and ASan aborts the
# run: a real finding truncate and shortreport already measure, so kbdtest declines that
# one case. Either spelling is the fix: #359's `byte_index >= len`, or report_length from
# upstream's readability pass (896e903), which the fork follows from 637b985. A tree with
# neither reads as unbounded and the 8BitDo stays out.
KBD_BOUNDED := $(call probe,hid_report.c,byte_index >= (len|report_length),HARNESS_BOUNDED_BITMAP)

# Does the target keep a bitmap whose usage range is wider than the block has bits for,
# instead of demanding one usage per bit exactly? Nothing here IS the compile
# prerequisite: the change is a predicate in the keyboard descriptor handler, with
# no symbol the harness links against, so the grep is a proxy. Every form of the fix
# shares the width arm `>= NKRO_MIN_BITS` on the field's width, spelled `size` on the
# fork from 524b61d through 637b985 (and on PR #366 as sent) and `key_bits` once upstream
# merged #366 as is_nkro_key_field() (e5f8ae8), which the fork follows from 28dd847. Any
# other spelling reads as "not fixed": the Keychron rows then assert the old zeros and
# fail as MISMATCH with the report bytes printed, loudly, the opposite of the silent skip
# c6d0264 removed.
KBD_WIDE := $(call probe,hid_report.c,(size|key_bits) >= NKRO_MIN_BITS,HARNESS_WIDE_USAGE_RANGE)

# Does _extract_kbd_other stop at the bytes that arrived? Its key_array loop is indexed by
# byte offset from the descriptor, so a report one byte short of the last key slot reads
# past it. DeskHop Extended guards the loop with the report length; PR #359 bounds the
# bitmap walk (the flag above) but not this loop, so the two need separate probes. An
# expression, not an identifier: the guard adds no name.
KBD_OTHER_BOUNDED := $(call probe,hid_report.c,i < MAX_KEYS && i < len,HARNESS_BOUNDED_KEY_ARRAY)

# Which names does nkro_block_t give a block's position and width? #359 called them offset
# and size; upstream's readability pass (896e903) renamed them offset_bits and size_bits.
# Only dump prints them, so this is a display concern, not a decode one.
NKRO_BITS_FIELDS := $(call probe,include/hid_parser.h,offset_bits,HARNESS_NKRO_BITS_FIELDS)

$(GEN)/lifted_kbd.c: $(SRC)/src/keyboard.c tools/lift.py $(OUT)/probes | $(GEN)
	@python3 tools/lift.py $< $@ $(KBD_LIFT)

$(GEN)/lifted_mouse.c: $(SRC)/src/mouse.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ extract_value extract_report_values

# Does the target keep mouse buttons per interface rather than in one global? Where it
# does, extract_report_values() falls back to iface->mouse_buttons and mousetest can say
# so; where not, the cases would not compile. The grep asks the header because the field
# IS the compile prerequisite, in the file $(HDRS) copies, so the two cannot disagree.
# Asking mouse.c about the helper that fills it would be a proxy: rename it and the cases
# drop silently, mousetest's last line then false. A tree with the field but the old
# fallback fails loudly as MISMATCH rather than quietly as a skip.
MOUSE_IFACE_BTN := $(call probe,include/hid_parser.h,mouse_buttons,HARNESS_IFACE_MOUSE_BUTTONS)

# Does the parser stop advancing its usage cursor once usages[] is full? Either spelling
# is the fix: PR #361's usages_left(), or upstream 1e31d10's pointer comparisons against
# usages + HID_MAX_USAGES. The pre-fix parser checks only usage_count and the element
# index against HID_MAX_USAGES, so a Report Count in the hundreds against one usage, the
# Gameball's shape, walks p_usage out of the array and the parser out of its own state;
# the Magic Trackpad's mouse interface declares one and stays out of mousetest and
# shortreport on such a tree rather than taking the run down.
PARSER_BOUNDED := $(call probe,hid_parser.c,usages_left|usages \+ HID_MAX_USAGES,HARNESS_BOUNDED_USAGES)

# Can get_report_value() read a field 32 bits wide? No tree can yet: (1u << size) - 1 is
# undefined at 32 and UBSan aborts the run, so the Corsair Scimitar's 32-button rows wait
# behind this flag. The grep guesses at the fix, a width test before the shift; a fix
# spelled differently reads as "not fixed" and keeps the device out, which fails safe:
# nothing is asserted rather than something wrong.
FIELD_32 := $(call probe,hid_report.c,size >= 32,HARNESS_FIELD_32)

# The two receivers PR #358 changes. Lifted rather than reimplemented for the same
# reason as everything else here: a hand copy would answer the question "does this
# PR do anything" with whatever the copy happened to say. The senders one level
# below them are NOT lifted - see src/recorders.c for where the cut is and why.
$(GEN)/lifted_cc.c: $(SRC)/src/keyboard.c tools/lift.py | $(GEN)
	@python3 tools/lift.py $< $@ process_consumer_report process_system_report

# A handler table keyed by value is read through get_report_handler(); src/handlers.h
# picks the matching accessor so dump, cctest and kbdtest measure either shape.
HANDLER_LOOKUP := $(call probe,hid_report.c,get_report_handler,HARNESS_HANDLER_LOOKUP)
CFLAGS += $(HANDLER_LOOKUP)

# Upstream ce8abb6 answers the same finding a third way: a 256-entry map of receiver ids
# per interface, resolved through report_receivers[] in hid_report.c, so a uint8_t ID can
# never miss the table. The table's name is the grep, and src/handlers.h reads through it.
HANDLER_MAP := $(call probe,hid_report.c,report_receivers,HARNESS_HANDLER_MAP)
CFLAGS += $(HANDLER_MAP)

# usb.c's routing is the body of tuh_hid_report_received_cb, lifted whole. src/routing.c
# supplies the two TinyUSB host calls and the global it reaches, and the receivers it
# calls are the recording stubs, so the answer is the firmware's on every tree and nothing
# is modelled (src/dispatch.h says why). A tree that factored the decision out as
# pick_receiver() has the callback call it, so that and its helper are lifted in front of
# it where they exist; a name lift.py cannot find fails the build, as intended.
DISPATCH_LIFT := $(if $(call has,usb.c,report_carries_id),report_carries_id) \
                 $(if $(call has,usb.c,process_report_f pick_receiver),pick_receiver) \
                 tuh_hid_report_received_cb

ROUTING := src/routing.c $(GEN)/lifted_dispatch.c

$(GEN)/lifted_dispatch.c: $(SRC)/src/usb.c tools/lift.py $(OUT)/probes | $(GEN)
	@python3 tools/lift.py $< $@ $(DISPATCH_LIFT)

# Every probe's answer in one file, whose timestamp moves only when an answer changes.
# The answers choose what is lifted and which cases compile in, but a grep is nothing make
# can see: a tree that gained or lost a lifted function kept the binary built the other
# way, and dispatchtest once printed "lifted" against a tree with nothing to lift. FORCE
# runs the recipe every time; cmp leaves the file, and everything built from it, alone
# while the answers hold. In DEPS, so every binary depends on it.
PROBE_ANSWERS := $(strip $(KBD_LIFT) $(KBD_MULTI) $(KBD_BOUNDED) $(KBD_WIDE) $(KBD_OTHER_BOUNDED) \
                 $(NKRO_BITS_FIELDS) $(MOUSE_IFACE_BTN) $(PARSER_BOUNDED) $(FIELD_32) \
                 $(HANDLER_LOOKUP) $(HANDLER_MAP) $(DISPATCH_LIFT))

.PHONY: FORCE
FORCE:

$(OUT)/probes: FORCE | $(GEN)
	@echo '$(PROBE_ANSWERS)' | cmp -s - $@ 2>/dev/null || echo '$(PROBE_ANSWERS)' > $@

DEPS += $(OUT)/probes

$(GEN)/hid_parser_instr.c: $(PARSER) tools/instrument.py | $(GEN)
	@python3 tools/instrument.py $< $@

# ---- binaries ----------------------------------------------------------------

$(OUT)/dump: src/dump.c src/handlers.h descriptors.h $(HDRS) $(DEPS) $(CORE) | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) $(NKRO_BITS_FIELDS) -o $@ src/dump.c $(CORE)

$(OUT)/mousetest: src/mousetest.c src/cases_mouse.h src/kept_out.h src/dispatch.h src/handlers.h descriptors.h $(HDRS) $(DEPS) $(CORE) $(GEN)/lifted_mouse.c $(ROUTING) | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) $(MOUSE_IFACE_BTN) $(PARSER_BOUNDED) $(FIELD_32) -o $@ src/mousetest.c $(GEN)/lifted_mouse.c $(ROUTING) $(CORE)

# no lifting here: extract_kbd_data and its helpers are all in hid_report.c,
# which $(CORE) already carries
$(OUT)/kbdtest: src/kbdtest.c src/cases_kbd.h src/kept_out.h src/handlers.h descriptors.h $(HDRS) $(DEPS) $(CORE) | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) $(KBD_MULTI) $(KBD_BOUNDED) $(KBD_WIDE) $(KBD_OTHER_BOUNDED) $(NKRO_BITS_FIELDS) -o $@ src/kbdtest.c $(CORE)

$(OUT)/exhaust: src/exhaust.c descriptors.h $(HDRS) $(DEPS) $(CORE) | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) -o $@ src/exhaust.c $(CORE)

$(OUT)/truncate: src/truncate.c descriptors.h $(HDRS) $(DEPS) $(CORE) | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) -o $@ src/truncate.c $(CORE)

# The one target that does NOT link src/stubs.c's consumer and system stubs:
# -DHARNESS_LIFT_CC keeps them out, and $(GEN)/lifted_cc.c supplies the real bodies
# from the branch under test. src/recorders.c supplies what those bodies reach for.
$(OUT)/cctest: src/cctest.c src/cases_cc.h src/handlers.h descriptors.h $(HDRS) $(DEPS) $(CORE) \
               $(GEN)/lifted_cc.c src/recorders.c | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) -DHARNESS_LIFT_CC -o $@ src/cctest.c \
	    $(GEN)/lifted_cc.c src/recorders.c $(CORE)

# Routing, not decode, so the only thing it needs out of $(CORE) is the four
# distinguishable receiver addresses in src/stubs.c. $(ROUTING) is the target's own
# callback and the harness's stand-ins for what it reaches.
$(OUT)/dispatchtest: src/dispatchtest.c src/dispatch.h src/handlers.h descriptors.h $(HDRS) $(DEPS) $(CORE) \
                     $(ROUTING) | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) -o $@ src/dispatchtest.c $(ROUTING) $(CORE)

# needs lifted_mouse.c: it drives extract_report_values, mousetest's entry point, so the
# truncated reports go through the firmware's own extraction. $(KBD_BOUNDED) as well, so
# both users of cases_kbd.h see the same device list rather than the 8BitDo in one binary
# and not the other; on an unbounded tree it would only add the overread truncate counts.
$(OUT)/shortreport: src/shortreport.c src/cases_mouse.h src/cases_kbd.h src/kept_out.h descriptors.h $(HDRS) $(DEPS) \
                    $(CORE) $(GEN)/lifted_mouse.c | $(GEN) check-target
	$(CC) $(CFLAGS) $(SAN) $(INCS) $(KBD_BOUNDED) $(KBD_OTHER_BOUNDED) $(NKRO_BITS_FIELDS) $(PARSER_BOUNDED) $(FIELD_32) -o $@ src/shortreport.c \
	    $(GEN)/lifted_mouse.c $(CORE)

# no ASan: this one is a stopwatch, and the fuzzer clamps rather than faults. -O2 in
# place of CFLAGS, so the two probes CFLAGS carries are passed by hand.
$(OUT)/timing: src/timing.c $(HDRS) $(DEPS) $(CORE) | $(GEN) check-target
	$(CC) -O2 $(WARN) $(INCS) $(HANDLER_LOOKUP) $(HANDLER_MAP) -o $@ src/timing.c $(CORE)

$(OUT)/fuzz: src/fuzz.c $(HDRS) $(DEPS) $(GEN)/hid_parser_instr.c $(REPORT) src/stubs.c $(GEN)/lifted_kbd.c | $(GEN) check-target
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

# Materialise a reference commit, build the same harness against it, and diff the parse of
# every descriptor: proof that a parser change is inert on known good devices while fixing
# the broken one. The tree is keyed by resolved SHA, not the name REF was spelled with:
# keyed by name the archive is extracted once and never again (the rule has no changing
# prerequisite), so every later `compare REF=main` would silently diff against whatever
# main pointed at the first time. Keyed by SHA, a moved branch is a path not yet there.
#
# Only defined when compare is asked for: these names appear in rule targets, which make
# expands while reading the file, so an unconditional definition runs `git rev-parse` on
# every invocation, `make clean` included. Nothing else needs a git repo at $(DESKHOP);
# the sub-make overrides TAG to resolve through the ordinary $(OUT) rules without REF_SHA.
ifneq ($(filter compare,$(MAKECMDGOALS)),)

REF_SHA  := $(shell git -C $(DESKHOP) rev-parse --short $(REF) 2>/dev/null)
REF_TREE := $(B)/tree/$(REF_SHA)
REF_OUT  := $(B)/ref-$(REF_SHA)

.PHONY: check-ref
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

# The regression gate: everything that must pass against *any* firmware worth shipping, so
# a red `make test` means the harness moved or a known good device stopped decoding. Safe
# to wire into CI. fuzz, truncate, shortreport and dispatch are deliberately NOT here:
# they fail by design on firmware that has the bug they look for (truncate on every tree
# measured, dispatch on upstream main today), so folding them in would leave this
# permanently red; their exit status is the finding, and `make findings` runs them and
# reports rather than gates. check-cli runs the replay tools' command lines at full
# length only, so it holds on any tree the decode suites hold on; check-constants is the
# only one needing the Pico SDK submodule and skips cleanly without it; test-sleepwake
# closes the list and needs only gcc.
.PHONY: test findings
test: mouse kbd consumer check-parse check-cli check-constants test-sleepwake
	@echo
	@echo "known good decode unchanged against $(SRC)"

# The Sleep/Wake rig's own gesture and delivery logic, on the host. Nothing here reads a
# DeskHop tree, so this target says nothing about decode; the rig's packets are covered on
# the DeskHop side by the consumer suite and by dispatch.
.PHONY: test-sleepwake
test-sleepwake:
	@bash emu/sleepwake/test.sh

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

# harness.h hand-copies TinyUSB's item tags and usages. Nothing in a normal build checks
# that copy, and a wrong value would not fail to compile: it would shift an offset and
# make every target report a plausible wrong answer. Deliberately not a prerequisite of
# `all`: nothing else needs the Pico SDK submodule, and requiring it would break builds.
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

# Rewrite the table of every entry in CORPUS.md from descriptors.h and the case tables.
# Refuses if an entry has no hand-kept row in tools/corpus_table.py, so the table stays
# complete rather than quietly short.
corpus:
	@python3 tools/corpus_table.py

# add_descriptor.py is the way a descriptor gets into the corpus, and its reader used
# to drop whole lines of a dump without saying so. A short descriptor does not fail to
# build either - it parses cleanly and describes a device nobody owns. Cheap to check
# and it needs nothing outside the repo, so unlike check-constants there is no skip.
check-parse:
	@python3 tools/add_descriptor.py --selftest

# The replay tools' command lines: the entry selector, the decimal argument parse and the
# usage paths, at full length only, so this holds on any tree the decode suites hold on.
# A truncated replay stays out for the reason fuzz and truncate do.
check-cli: $(OUT)/shortreport $(OUT)/truncate $(OUT)/dump
	@bash tools/check_cli.sh $(OUT)

clean:
	rm -rf $(B)
