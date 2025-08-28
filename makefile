t:
	$(CC) test/main.c -DHTTP_IMPL -DHTTP_PTHREAD -I./ -o test/main
	./test/main
