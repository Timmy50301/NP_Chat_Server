all:
	g++ -g chat_server.cpp -o chat_server
.PONY : clean
clean:
	rm -f *.exe *.out *.txt chat_server