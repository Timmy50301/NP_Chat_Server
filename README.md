# Chat Room Server

It is a concurrent connection-oriented server which allows clients to connect.
In this system, users can communicate with other users.
The server support all commands in [link](https://github.com/Timmy50301/NP_Linux_Shell), and some extra built-in command.

##  Usage

To make the sample program:
```bash
make
```

Run the server on 140.113.194.210 and port 5000
```bash
bash$ ./chat_server 5000
```

Assume your server is running on 140.113.194.210 and listening at port 5000.
```bash
bash$ telnet 140.113.194.210 5000
```

"% " is the command line prompt.
```bash
% name Pikachu # change your name by this command.
```
```bash
% who # show information of all users.
```
```bash
% tell 3 Hello World. # send messages to other using this format "tell <user id> <message>"
```
```bash
% yell Good morning everyone. # broadcast the message using this format "yell <message>"
```
```bash
% exit # the shell terminates after receiving the exit command
```

