# Altere esta linha para o arquivo da task que voce quer compilar.
#
# Exemplos:
#   Tarefa 2.1: obj-m += serial_write.o
#   Tarefa 2.2: obj-m += serial_write.o
#   Tarefa 2.3: obj-m += serial_write.o
#   Tarefa 2.4: obj-m += serial_write.o
#   Tarefa 3.1: obj-m += serial_write.o
# obj-m += serial_write.o
# obj-m += serial_read.o
obj-m += sysfs.o

PWD := $(CURDIR)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
