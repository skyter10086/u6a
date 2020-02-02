MANPATH_MAP = $(shell cat $(MANPATH_CONF) | grep ^MANPATH_MAP | grep -w -m1 $(prefix)/bin)
MAN_DIR     = $(word 3, $(MANPATH_MAP))
MAN1_DIR    = $(MAN_DIR)/man1
MAN_FILES   = u6ac.1 u6a.1

install: $(MAN_FILES)
	cp $^ $(MAN1_DIR) && $(REBUILD_MANDB) $(MAN_DIR)

uninstall: $(MAN_FILES)
	pushd $(MAN1_DIR) && rm -f $^ && popd && $(REBUILD_MANDB) $(MAN_DIR)
