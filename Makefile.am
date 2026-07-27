## $Id$

include $(top_srcdir)/Makefile.am.global

ACLOCAL_AMFLAGS=-I m4

.PHONY: test abicheck

SUBDIRS = mpglib libmp3lame frontend Dll doc include misc dshow ACM \
	mac vc_solution test

CLEANFILES = testcase.new.mp3

EXTRA_DIST = \
	.clang-format \
	API \
	DEFINES \
	HACKING \
	INSTALL.configure \
	LICENSE \
	Makefile.MSVC \
	Makefile.am.global \
	STYLEGUIDE \
	USAGE \
	configMS.h \
	debian \
	lame.bat \
	lame.spec.in \
	lame.spec \
	testcase.mp3 \
	testcase.wav \
	maintainer/abicheck.sh \
	maintainer/abi/libmp3lame.abi \
	maintainer/check-dist.sh \
	maintainer/check-dist.ps1 \
	maintainer/gen-api-docs.sh \
	maintainer/gen-build-matrix.sh \
	maintainer/gen-build-matrix.ps1 \
	maintainer/setup-windows-deps.ps1 \
	maintainer/perf-compare.sh \
	maintainer/setup-quality-corpus.sh \
	maintainer/quality-compare.py \
	maintainer/gen-coverage-matrix.sh \
	maintainer/coverage-run.sh \
	maintainer/coverage-mkinputs.py \
	maintainer/coverage-report.py \
	maintainer/coverage-workload.txt

#
# The differences depend on the used processor architecture, the used
# compiler and the used options for the compiler, so make test may
# show some differences. You should only be concerned if you are a
# LAME developer and the number of differences change after you
# modified the source.
#
testcase.new.mp3: testcase.wav frontend/lame$(EXEEXT)
	time frontend/lame$(EXEEXT) --nores $(top_srcdir)/testcase.wav testcase.new.mp3 || $(RM_F) testcase.new.mp3

test: testcase.mp3 testcase.new.mp3
	@echo
	@echo "The following output has value only for a LAME-developer, do not make _any_"
	@echo "assumptions about what this number means. You do not need to care about it."
	@cmp -l testcase.new.mp3 $(top_srcdir)/testcase.mp3 | wc -l

testupdate: testcase.new.mp3
	cp testcase.new.mp3 $(top_srcdir)/testcase.mp3

#
# Check the exported ABI of the library that was just built against the
# contract committed in the source tree. Needs no audio input and no reference
# build; the deepest of its checks additionally wants libabigail and a build
# carrying debug information, and reports itself skipped without them.
#
abicheck:
	@$(SHELL) $(top_srcdir)/maintainer/abicheck.sh \
		-b $(top_builddir)/libmp3lame/.libs \
		-i $(top_srcdir)/include \
		-a $(top_srcdir)/maintainer/abi/libmp3lame.abi

testg: frontend/mp3x$(EXEEXT) $(top_srcdir)/../test/castanets.wav
	frontend/mp3x$(EXEEXT) -h $(top_srcdir)/../test/castanets.wav

update:
	cd $(top_srcdir) && svn update || true

diff:
	cd $(top_srcdir) && svn diff || true

frontend/lame$(EXEEXT):
	$(MAKE) $(MAKEFLAGS)

frontend/mp3x$(EXEEXT): frontend/lame$(EXEEXT)

