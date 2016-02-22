# included mk file for chronos

CHRONOS_DIR := ${ROOT}/src
CHRONOS_TEST_DIR := ${ROOT}/tests

chronos:
	${MAKE} -C ${CHRONOS_DIR}

chronos_test:
	${MAKE} -C ${CHRONOS_DIR} test

chronos_full_test:
	${MAKE} -C ${CHRONOS_DIR} full_test

chronos_clean:
	${MAKE} -C ${CHRONOS_DIR} clean

chronos_distclean: chronos_clean

.PHONY: chronos chronos_test chronos_clean chronos_distclean
