# -*- Mode: makefile-gmake -*-

all:
%:
	@$(MAKE) -C test_encode $*
	@$(MAKE) -C test_io $*
	@$(MAKE) -C test_parcel $*
	@$(MAKE) -C test_request $*
	@$(MAKE) -C test_transport $*

clean: unitclean
	rm -f coverage/*.gcov
	rm -fr coverage/report
