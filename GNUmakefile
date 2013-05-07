all: build/boldwatch_noseconds.pbw build/boldwatch_noseconds_nodate.pbw build/boldwatch_nodate.pbw build/boldwatch.pbw

clean:
	./waf clean

build/boldwatch_noseconds.pbw: src/boldwatch.c
	echo '#define BOLDWATCH_SECONDS false' > src/config.h
	echo '#define BOLDWATCH_DATE true' >> src/config.h
	echo '#define APP_NAME "Boldwatch (NS)"' >> src/config.h
	echo '#define MY_UUID { 0x9A, 0x8C, 0x21, 0x23, 0x7D, 0x20, 0x43, 0x24, 0xA1, 0x85, 0x45, 0x69, 0x2A, 0x58, 0x0E, 0xC9 }' >> src/config.h
	./waf build && mv build/boldwatch.pbw $@

build/boldwatch_noseconds_nodate.pbw: src/boldwatch.c
	echo '#define BOLDWATCH_SECONDS false' > src/config.h
	echo '#define BOLDWATCH_DATE false' >> src/config.h
	echo '#define APP_NAME "Boldwatch (NS,ND)"' >> src/config.h
	echo '#define MY_UUID { 0x9A, 0x8C, 0x21, 0x23, 0x7D, 0x20, 0x43, 0x24, 0xA1, 0x85, 0x45, 0x69, 0x2A, 0x58, 0x0E, 0xC8 }' >> src/config.h
	./waf build && mv build/boldwatch.pbw $@

build/boldwatch_nodate.pbw: src/boldwatch.c
	echo '#define BOLDWATCH_SECONDS true' > src/config.h
	echo '#define BOLDWATCH_DATE false' >> src/config.h
	echo '#define APP_NAME "Boldwatch (ND)"' >> src/config.h
	echo '#define MY_UUID { 0x9A, 0x8C, 0x21, 0x23, 0x7D, 0x20, 0x43, 0x24, 0xA1, 0x85, 0x45, 0x69, 0x2A, 0x58, 0x0E, 0xC7 }' >> src/config.h
	./waf build && mv build/boldwatch.pbw $@

build/boldwatch.pbw: src/boldwatch.c
	echo '#define BOLDWATCH_SECONDS true' > src/config.h
	echo '#define BOLDWATCH_DATE true' >> src/config.h
	echo '#define APP_NAME "Boldwatch"' >> src/config.h
	echo '#define MY_UUID { 0x9A, 0x8C, 0x21, 0x23, 0x7D, 0x20, 0x43, 0x24, 0xA1, 0x85, 0x45, 0x69, 0x2A, 0x58, 0x0E, 0xC6 }' >> src/config.h
	./waf build

