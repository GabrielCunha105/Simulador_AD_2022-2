all: simulador

simulador: simulador.c
	gcc simulador.c -o simulador -lm -O2

run: simulador
	./simulador

clear:
	rm ./simulador