# Included mk file for the cpp-common module

CPP_COMMON_DIR := ${MODULE_DIR}/cpp-common/scripts/stats-c

cpp-common: 
	${MAKE} -C ${CPP_COMMON_DIR}

cpp-common_test:
	@echo "No tests for cpp-common"

cpp-common_clean:
	${MAKE} -C ${CPP_COMMON_DIR} clean

cpp-common_distclean:
	${MAKE} -C ${CPP_COMMON_DIR} clean

.PHONY: cpp-common cpp-common_test cpp-common_clean cpp-common_distclean
