MAN_DIR = $(prefix)/share/man/man1
MAN_FILES = u6ac.1 u6a.1

install: $(MAN_FILES)
	mkdir -p $(MAN_DIR) && cp -t $(MAN_DIR) $^

uninstall: $(MAN_FILES)
	pushd $(MAN_DIR) && rm -f $^ && popd
