OBJ=helloworld.o

obj-m += $(OBJ)
MOD_DIR=/local/comp3301/linux-3.9.4/

.PHONY: all
all:
	make -C $(MOD_DIR) M=$(PWD) ARCH=um modules

.PHONY: clean
clean:
	make -C $(MOD_DIR) M=$(PWD) ARCH=um clean
