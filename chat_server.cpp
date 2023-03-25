#include <iostream>
#include <vector>
#include <string>
#include <sstream>      //istringstream
#include <unistd.h>     //close()
#include <stdio.h>      //fopen
#include <unistd.h>     //execv
#include <cstring>      //strcpy
#include <sys/wait.h>   //wait
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <netdb.h>
#include <iomanip>      //setfill()
#include <stdlib.h>     //clearenv()
#include <arpa/inet.h>
#include <sys/types.h>  //open()
#include <sys/stat.h>   //open()
#include <fcntl.h>      //open()

#define MaxClientSize 30
//maximum message length=15000

using namespace std;

struct Pipe
{
    int count;      //when pipe's count == 0 read by next cmd
    int senderId;
    int recverId;
    int clientId;
    int * pipefd;
    string sign;    //sign = {"NONE", "PIPE", "NUM_PIPE", "ERR_PIPE", "REDIR", "USER_PIPE"}
};
struct Fd
{
    int in;
    int out;
    int error;
};
struct Env
{
    string name;
    string value;
};
struct ClientInfo
{
    int id;
    string name;
    string ip;
    unsigned short port;
    int clientFD;
    vector<struct Env> envV;
};

//Set argv to exec
char** SetArgv(string cmd, vector<string> arg_vec)
{   
    char ** argv = new char* [arg_vec.size()+2];
    argv[0] = new char [cmd.size()+1];
    strcpy(argv[0], cmd.c_str());
    for(int i=0; i<arg_vec.size(); i++)
    {
        argv[i+1] = new char [arg_vec[i].size()+1];
        strcpy(argv[i+1], arg_vec[i].c_str());
    }
    argv[arg_vec.size()+1] = NULL;
    return argv;
}
vector<string> SplitEnvPath(string path)
{
    vector<string> path_vec;
    string temp;
    stringstream ss(path);
    
    while(getline(ss, temp, ':')) path_vec.push_back(temp);

    return path_vec;
}
bool LegalCmd(string cmd, vector<string> arg_vec, vector<string> path_vec)
{
    for(int i=0 ; i<path_vec.size() ; i++)
    {
        string path_temp = path_vec[i] + "/" + cmd;
        FILE* file = fopen(path_temp.c_str(), "r");
        if(file != NULL)
        {
            fclose(file);
            return true;
        }
    }
    return false;
}

class SERVER
{
private:
    //
    bool Client_ID[MaxClientSize];
    vector<struct ClientInfo>  Client_Table_vec;
    fd_set afds;
    int serverFD, nfds;
    //
    string input;
    vector<string> input_vec;
    vector<string>::iterator iterLine;

    string path;
    vector<string> path_vec;
    vector<struct Pipe> pipe_vec;
    vector<string> arg_vec;

    Fd fd;
    string cmd;
    string sign;
    int count;
    int writeId;
    int readId;
    int waittimes;      //how many child process needs to wait

public:
    SERVER();
    //
    void START_SERVER(int _port);
    int Connect_TCP(int port);
    void InitEnv(vector<struct ClientInfo>::iterator client);
    int SelectId();
    void WelcomeUser(int clientFD);
    vector<struct ClientInfo>::iterator IdentifyClientById(int id);
    vector<struct ClientInfo>::iterator IdentifyClientByFd(int fd);
    void RemoveClient(vector<struct ClientInfo>::iterator client);
    void BroadCast(vector<struct ClientInfo>::iterator iter, string msg, string action, int id);
    //
    void Execute_one_input(string input, vector<struct ClientInfo>::iterator &client);
    void SETUP(vector<struct ClientInfo>::iterator client);
    void DoBuildinCmd(vector<struct ClientInfo>::iterator &client);
    void SetEnvTable(vector<struct Env> &envV, string env, string assign);
    void ConnectFD(int writeId, int readId, vector<struct ClientInfo>::iterator client, bool UserPipeInError, bool UserPipeOutError);
    void DoCmd(int clientFD);

    void CreatePipe(int clientId, int senderId, int recverId);
    void ReduceCount_Ord(int clientId);
    void ReduceCount_NUM_ERR(int clientId);
    void ClosePipe(int clientId, int readId);
    bool PipeExist(int clientId);
    bool UserPipeExist(int senderId, int recverId);
    bool WaitChild();   //decide weather need to wait or not
};
SERVER::SERVER()
{
    setenv("PATH", "bin:.", 1);
    path = getenv("PATH");
    path_vec = SplitEnvPath(path);
    // for(int i=0 ; i<path_vec.size() ; i++){
    //     cout<<path_vec[i]<<endl;
    // }
    waittimes=0;
}

void SERVER::START_SERVER(int _port)
{
    sockaddr_in cliAddr;
    socklen_t cliLen;
    int port = _port;
    fd_set rfds;

    memset((void*)Client_ID, (int)false, MaxClientSize * sizeof(bool));  //Initilize Client_ID to false, cause no one is connected yet 

    serverFD = Connect_TCP(port);   //TCP server: socket() setsockopt() bind() listen()
    // PrintServer(serverFD);

    signal(SIGCHLD, SIG_IGN);

    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(serverFD, &afds);   //afds[serverFD] = true
    while(1)
    {
        memcpy(&rfds, &afds, sizeof(rfds));
        // PrintUsers();
        // PrintAfds();

        /*  if there is a client enter, then keep going, otherwise just stop here   */
        cout<<"Wait for next move..."<<endl;
        if(select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (timeval*)0) <0)
        {
            if(errno == EINTR)
                continue;
            exit(1);
        }

        if(FD_ISSET(serverFD, &rfds))
        {
            int sSock;
            cliLen = sizeof(cliAddr);
            sSock = accept(serverFD, (struct sockaddr*)&cliAddr, &cliLen);
            
            //record client info
            ClientInfo client;
            client.id = SelectId();                         //check which id can use when client login
            client.name = "(no name)";
            client.ip = inet_ntoa(cliAddr.sin_addr);
            client.port = ntohs(cliAddr.sin_port);
            client.clientFD = sSock;
            Env env;
            env.name = "PATH";
            env.value = "bin:.";
            client.envV.push_back(env);
            Client_Table_vec.push_back(client);

            FD_SET(sSock, &afds);

            // PrintUserLogin();
            WelcomeUser(sSock);
            BroadCast(Client_Table_vec.end()-1, "", "login", 0);
            
            string title = "% ";
            write(sSock, title.c_str(), title.length());
        }

        for(int fd=0; fd<nfds; fd++)
        {
            if(fd != serverFD && FD_ISSET(fd, &rfds))
            {
                char _input[15000];                             //maximum message length=15000
                memset(&_input, '\0', sizeof(_input));
                vector<ClientInfo>::iterator client = IdentifyClientByFd(fd);
                if(read(fd, _input, 15000) <0)                  //read _input from a certain client
                {
                    if(errno == EINTR)
                        continue;
                }
                else
                {
                    //########### here ###########
                    InitEnv(client);                            //setup the env which is saved in ClientInfo
                    Execute_one_input(string(_input), client);  //execute one input from a certain client
                    if(cmd == "exit" || cmd == "EOF")
                    {
                        // client exit
                        string logoutname=client->name;
                        RemoveClient(client);
                        close(fd);
                        FD_CLR(fd, &afds);
                        BroadCast(Client_Table_vec.end(), logoutname, "logout", 0);
                    }
                }
            }
        }
    }
}

int SERVER::Connect_TCP(int port)
{
    struct sockaddr_in servAddr;
    struct protoent *proEntry;
    int serverFD, type;

    bzero((char*)&servAddr, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = INADDR_ANY;
    servAddr.sin_port = htons(port);

    int optval = 1;
    serverFD = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));   //reuse port
    bind(serverFD, (struct sockaddr*)&servAddr, sizeof(servAddr));
    listen(serverFD, 0);

    return serverFD;
}
void SERVER::WelcomeUser(int clientFD)
{
    string msg = "****************************************\n";
    write(clientFD, msg.c_str(), msg.length());
    msg = "** Welcome to the information server. **\n";
    write(clientFD, msg.c_str(), msg.length());
    msg = "****************************************\n";
    write(clientFD, msg.c_str(), msg.length());
}
int SERVER::SelectId()     //check which id can use when client login
{
    for(int i=0; i<MaxClientSize; i++)
    {
        if(!Client_ID[i])
        {
            Client_ID[i] = true;
            return i+1;
        }
    }
    return 0;
}
void SERVER::InitEnv(vector<struct ClientInfo>::iterator client)    //setup the env which is saved in ClientInfo
{
    clearenv(); //Linux only
    vector<struct Env>::iterator iter = client->envV.begin();
    while(iter != client->envV.end())
    {
        setenv(iter->name.c_str(), iter->value.c_str(), 1);
        iter++;
    }
}
vector<struct ClientInfo>::iterator SERVER::IdentifyClientById(int id)
{
    vector<struct ClientInfo>::iterator iter = Client_Table_vec.begin();
    while(iter != Client_Table_vec.end())
    {
        if(iter->id == id)
            return iter;
        iter++;
    }
    return iter;
}
vector<struct ClientInfo>::iterator SERVER::IdentifyClientByFd(int fd)
{
    vector<struct ClientInfo>::iterator iter = Client_Table_vec.begin();
    while(iter != Client_Table_vec.end())
    {
        if(iter->clientFD == fd)
            return iter;
        iter++;
    }
    return iter;
}
void SERVER::RemoveClient(vector<struct ClientInfo>::iterator client)   //close all pipe related to client
{
    for(int i=0 ; i<pipe_vec.size() ; i++)
    {
        if(pipe_vec[i].clientId == client->id || pipe_vec[i].recverId == client->id)
        {
            close(pipe_vec[i].pipefd[0]);
            close(pipe_vec[i].pipefd[1]);
            delete [] pipe_vec[i].pipefd;
            pipe_vec.erase(pipe_vec.begin()+i);
            i--;
        }
    }
    Client_ID[client->id-1] = false;
    Client_Table_vec.erase(client);
}
void SERVER::BroadCast(vector<struct ClientInfo>::iterator iter, string msg, string action, int id)
{
    string news;
    vector<struct ClientInfo>::iterator partner;
    if(id != 0)
        partner = IdentifyClientById(id);

    if(action == "login")
        news = "*** User '" + iter->name + "' entered from " + iter->ip + ":" + to_string(iter->port) + ". ***\n";
    else if(action == "logout")
        news = "*** User '" + msg + "' left. ***\n";
    else if(action == "yell")
        news = "*** " + iter->name + " yelled ***:" + msg + "\n";
    else if(action == "name")
        news = "*** User from " + iter->ip + ":" + to_string(iter->port) + " is named '" + msg + "'. ***\n";
    else if(action == "writeuser")
    {   
        /*  erase null at the last character    */
        while(!msg.empty() && (msg[msg.size()-1] == '\r' || msg[msg.size()-1] == '\n'))
            msg.erase(msg.size()-1);
        news = "*** " + iter->name + " (#" + to_string(iter->id) + ") just piped '" + msg + "' to " + partner->name + " (#" + to_string(partner->id) + ") ***\n";
    }
    else if(action == "readuser")
    {
        /*  erase null at the last character    */
        while(!msg.empty() && (msg[msg.size()-1] == '\r' || msg[msg.size()-1] == '\n'))
            msg.erase(msg.size()-1);
        news = "*** " + iter->name + " (#" + to_string(iter->id) + ") just received from " + partner->name + " (#" + to_string(partner->id) + ") by '" + msg + "' ***\n";
    }

    for(int fd=0; fd<nfds; fd++)
    {
        if(fd != serverFD && FD_ISSET(fd, &afds))
        {
            write(fd, news.c_str(), news.length());
        }
    }
}

void SERVER::Execute_one_input(string input, vector<struct ClientInfo>::iterator &client)                           //path, path_vec, pipe_vec, waittimes stay globally
{   
    path = getenv("PATH");
    path_vec = SplitEnvPath(path);

    input_vec.clear();
    istringstream ss(input);
    string temp;
    while(ss>>temp) input_vec.push_back(temp);          //input_vec = command line input seperated by " "

    iterLine = input_vec.begin();
    while(iterLine != input_vec.end() && *iterLine != "\0")
    {
        fd = {0, client->clientFD, client->clientFD};
        arg_vec.clear();
        sign = "NONE";
        count = 0;              //表示第一個碰到pipe sign( | |n !n ) 要pipe到幾個command之後
        writeId = 0;
        readId = 0;
        bool UserPipeInError = false;
        bool UserPipeOutError = false;

        SETUP(client);                //setup [cmd], [pipe_vec], [arg_vec], [sign], [count] untill first sign( < arg , | , |n , !n )
                                //iterator pointing to next
        
        if(cmd == "setenv" || cmd == "printenv" || cmd == "exit" || cmd == "EOF" || cmd == "who" || cmd == "tell" || cmd == "yell" || cmd == "name")
        {
            DoBuildinCmd(client);
            ClosePipe(client->id, readId);
            if(cmd == "exit" || cmd == "EOF") return;
            else break;
        }
        
        if(readId != 0)
        {
            if(!Client_ID[readId-1])
            {
                string msg = "*** Error: user #" + to_string(readId) + " does not exist yet. ***\n";
                write(client->clientFD, msg.c_str(), msg.length());
                UserPipeInError = true;
            }
            else if(!UserPipeExist(readId, client->id))
            {
                string msg = "*** Error: the pipe #" + to_string(readId) + "->#" + to_string(client->id) + " does not exist yet. ***\n";
                write(client->clientFD, msg.c_str(), msg.length());
                UserPipeInError = true;
            }
            else
                BroadCast(client, input, "readuser", readId);
        }
        if(sign == "REDIR")
        {
            string fileName;
            fileName = arg_vec[arg_vec.size()-1];
            arg_vec.pop_back();
            FILE* file = fopen(fileName.c_str(), "w");
            fd.out = fileno(file);
        }
        else if(sign == "USER_PIPE")
        {
            if(!Client_ID[writeId-1])
            {
                string msg = "*** Error: user #" + to_string(writeId) + " does not exist yet. ***\n";
                write(client->clientFD, msg.c_str(), msg.length());
                UserPipeOutError = true;
            }
            else if(!UserPipeExist(client->id, writeId))
            {
                BroadCast(client, input, "writeuser", writeId);
                count = -1;
                CreatePipe(client->id, client->id, writeId);
            }
            else
            {
                string msg = "*** Error: the pipe #" + to_string(client->id) + "->#" + to_string(writeId) + " already exists. ***\n";
                write(client->clientFD, msg.c_str(), msg.length());
                UserPipeOutError = true;
            }
        }

        ConnectFD(writeId, readId, client, UserPipeInError, UserPipeOutError);            //開始對接fd(暫存)與pipe的接口
        waittimes++;            //how many child process need to wait
        pid_t pid = fork();

        if(pid == 0)
        {
            DoCmd(client->clientFD);            //child process 將此process的stdin, stdout, stderr與暫存的fd對接，然後execv then it kill itself  
        }
                                         
        else
            if(waittimes%100 == 0) sleep(1);  //considering forking too quick

        if(fd.in != 0)          //??????close read from pipe, the other entrance is closed in ConnectFD
            close(fd.in);
        
        if(WaitChild())        //wait for child process only when after piping(count=0) & redirection
        {
            int status = 0;
            for(int i=1 ; i<=waittimes ; i++)       //need to wait for every child process
                waitpid(-1, &status, 0);
            
            waittimes=0;
        }
        
        ClosePipe(client->id, readId);                    //將count=0 的pipe關掉(pipefd[0], pipefd[1]) and also erase from pipe_vec

        if( (sign=="NUM_PIPE" || sign=="ERR_PIPE") && iterLine!=input_vec.end() )   //numpipe & errorpipe appear in the middle
            ReduceCount_NUM_ERR(client->id);      //將pipe_vec中所有 "NUM_PIPE", "ERR_PIPE" count--

        ReduceCount_Ord(client->id);              //將pipe_vec中所有 "PIPE" count--
    }

    if(input_vec.size()!=0)             //if input is not an enter ("\n" in UNIX)
        ReduceCount_NUM_ERR(client->id);

    string title = "% ";
    write(client->clientFD, title.c_str(), title.length());
    
}

void SERVER::SETUP(vector<struct ClientInfo>::iterator client)
{
    string temp;
    bool isCmd = true;

    bool IsUserCmd;
    if(input_vec[0]=="setenv" || input_vec[0]=="printenv" || input_vec[0]=="exit" || input_vec[0] == "EOF" || input_vec[0]=="who" || input_vec[0]=="tell" || input_vec[0]=="yell" || input_vec[0]=="name" )
        IsUserCmd=true;
    else
        IsUserCmd=false;
    
    while(iterLine != input_vec.end())
    {
        temp = *iterLine;

        if(temp[0] == '|' && temp.size() == 1 && IsUserCmd==false)
        {
            sign = "PIPE";
            count = 1;
            iterLine++;
            CreatePipe(client->id, 0, 0);
            break;
        }
        else if(temp[0] == '|' && temp.size() > 1 && IsUserCmd==false)
        {
            sign = "NUM_PIPE";
            count = stoi(temp.c_str()+1);
            iterLine++;
            if(!PipeExist(client->id))      //if read head connected to the same line with others, use the same pipe created before
                CreatePipe(client->id, 0, 0);
            break;
        }
        else if(temp[0] == '!' && IsUserCmd==false)
        {
            sign = "ERR_PIPE";
            count = stoi(temp.c_str()+1);
            iterLine++;
            if(!PipeExist(client->id))      //if read head connected to the same line with others, use the same pipe created before
                CreatePipe(client->id, 0, 0);
            break;
        }
        else if(temp[0] == '>' && temp.size() == 1 && IsUserCmd==false)
        {
            sign = "REDIR";
            iterLine++;
            arg_vec.push_back(*iterLine);

            iterLine++;
            break;
        }
        else if(temp[0] == '>' && temp.size() > 1 && IsUserCmd==false)
        {
            sign = "USER_PIPE";
            writeId = stoi(temp.c_str()+1);;
            
            /*  check cat >2 <1 case    */
            if((iterLine+1) != input_vec.end())
            {
                temp = *(iterLine+1);
                if(temp[0] == '<')
                {
                    iterLine++;
                    continue;
                }
            }
            iterLine++;
            break;
        }
        else if(temp[0] == '<' && IsUserCmd==false)
        {
            readId = stoi(temp.c_str()+1);
        }
        else if(isCmd)
        {
            cmd = temp;
            isCmd = false;
        }
        else
        {
            arg_vec.push_back(temp);
        }
        
        iterLine++;
    }
}
void SERVER::DoBuildinCmd(vector<struct ClientInfo>::iterator &client)
{
    if(cmd == "printenv")
    {
        char* msg = getenv(arg_vec[0].c_str());                 //arg_vec[0]= env name
        write(client->clientFD, msg, strlen(msg));
        write(client->clientFD, "\n", strlen("\n"));
        return;
    }
    else if(cmd == "setenv")
    {
        SetEnvTable(client->envV, arg_vec[0], arg_vec[1]);
        setenv(arg_vec[0].c_str(), arg_vec[1].c_str(), 1);      //arg_vec[0]= env name   arg_vec[1]= env value
        if(arg_vec[0] == "PATH")
        {
            path = getenv("PATH");
            path_vec.clear();
            path_vec = SplitEnvPath(path);
        }
        return;
    }
    else if(cmd == "who")
    {
        string msg = "<ID>    <nickname>    <IP:port>    <indicate me>\n";
        write(client->clientFD, msg.c_str(), msg.length());
        for(int i=0; i<MaxClientSize; i++)
        {
            if(Client_ID[i])
            {
                int id = i+1;
                vector<ClientInfo>::iterator iter = IdentifyClientById(id);
                msg = to_string(iter->id) + "    " + iter->name + "    " + iter->ip + ":" + to_string(iter->port) + "    ";
                write(client->clientFD, msg.c_str(), msg.length());
                if(id == client->id)
                {
                    write(client->clientFD, "<-me\n", strlen("<-me\n"));
                }
                else
                    write(client->clientFD, "\n", strlen("\n"));
            }
        }

        return;   
    }
    else if(cmd == "tell")
    {
        int sendId = atoi(arg_vec[0].c_str());
        string msg = "*** " + client->name + " told you ***:";
        for(int i=1; i<arg_vec.size(); i++)
            msg += (" " + arg_vec[i]);
        msg += "\n"; 

        vector<ClientInfo>::iterator receiver = IdentifyClientById(sendId);
        if(receiver != Client_Table_vec.end())
        {
            write(receiver->clientFD, msg.c_str(), msg.length()) <0;
        }
        else
        {
            msg = "*** Error: user #" + to_string(sendId) + " does not exist yet. ***\n";
            write(client->clientFD, msg.c_str(), msg.length());
        }
        
        return;
    }
    else if(cmd == "yell")
    {
        string msg;
        for(int i=0; i<arg_vec.size(); i++)
            msg += (" " + arg_vec[i]);
        
        BroadCast(client, msg, "yell", 0);
        
        return;
    }
    else if(cmd == "name")
    {
        string name = arg_vec[0];
        
        for(int i=0; i<MaxClientSize; i++)
        {
            if(Client_ID[i])
            {
                int id = i+1;
                vector<ClientInfo>::iterator iter = IdentifyClientById(id);
                if(iter->name == name && id != client->id)
                {
                    string msg = "*** User '" + name + "' already exists. ***\n";
                    write(client->clientFD, msg.c_str(), msg.length());
                    return;
                }
            }
        }

        BroadCast(client, name, "name", 0);
        client->name = name;
        return;
    }
    else                                                     // exit or EOF
        return;
}
void SERVER::SetEnvTable(vector<struct Env> &envV, string env, string assign)
{
    vector<struct Env>::iterator iter = envV.begin();
    while(iter != envV.end())
    {
        if(iter->name == env)
        {
            iter->value = assign;
            break;
        }
        iter++;
    }
    
    if(iter == envV.end())
    {
        struct Env temp;
        temp.name = env;
        temp.value = assign;
        envV.push_back(temp);
    }
}
void SERVER::ConnectFD(int writeId, int readId, vector<struct ClientInfo>::iterator client, bool UserPipeInError, bool UserPipeOutError)
{
    vector<struct Pipe>::iterator iter = pipe_vec.begin();
    bool setIn =false;
    bool setOut = false;

    if(sign == "PIPE")
    {
        if(UserPipeInError)
        {
            fd.in = -1;
            setIn = true;
        }
        while(iter != pipe_vec.end())
        {
            
            if(!setIn)
            {   /*  cmd <n  */
                if((*iter).senderId == readId && (*iter).recverId == client->id && (*iter).sign == "USER_PIPE")
                {
                    close((*iter).pipefd[1]);
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }/* |0  cmd or | cmd */
                else if((*iter).count == 0 && readId == 0 && (*iter).clientId == client->id)
                {
                    close((*iter).pipefd[1]);   //close write to pipe
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }
            }
            if(!setOut)
            {
                /*  cmd |   */
                if( (*iter).count==1 && (*iter).sign=="PIPE" && (*iter).clientId == client->id)
                {
                    fd.out = (*iter).pipefd[1];
                    setOut = true;
                }
            }
            iter++;
        }
    }
    else if(sign == "NUM_PIPE")
    {
        if(UserPipeInError)
        {
            fd.in = -1;
            setIn = true;
        }
        while(iter != pipe_vec.end())
        {
            if(!setIn)
            {   /*  cmd <n  */
                if((*iter).senderId == readId && (*iter).recverId == client->id && (*iter).sign == "USER_PIPE")
                {
                    close((*iter).pipefd[1]);
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }/* |0  cmd or | cmd */
                else if((*iter).count == 0 && readId == 0 && (*iter).clientId == client->id)
                {
                    close((*iter).pipefd[1]);   //close write to pipe
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }
            }
            if(!setOut)
            {
                /*  cmd |n  */
                if((*iter).count == count && (*iter).sign == "NUM_PIPE" && (*iter).clientId == client->id)
                {
                    fd.out = (*iter).pipefd[1];
                    setOut = true;
                }
            }
            iter++;
        }
    }
    else if(sign == "ERR_PIPE")
    {
        if(UserPipeInError)
        {
            fd.in = -1;
            setIn = true;
        }
        while(iter != pipe_vec.end())
        {
            if(!setIn)
            {   /*  cmd <n  */
                if((*iter).senderId == readId && (*iter).recverId == client->id && (*iter).sign == "USER_PIPE")
                {
                    close((*iter).pipefd[1]);
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }/* |0  cmd or | cmd */
                else if((*iter).count == 0 && readId == 0 && (*iter).clientId == client->id)
                {
                    close((*iter).pipefd[1]);   //close write to pipe
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }
            }
            if(!setOut)
            {
                /*  cmd !n  */
                if((*iter).count == count && (*iter).sign == "ERR_PIPE" && (*iter).clientId == client->id)
                {
                    fd.out = (*iter).pipefd[1];
                    fd.error = (*iter).pipefd[1];
                    setOut = true;
                }
            }
            iter++;
        }
    }
    else if(sign == "REDIR")
    {
        if(UserPipeInError)
        {
            fd.in = -1;
            setIn = true;
        }
        while(iter != pipe_vec.end())
        {
            if(!setIn)
            {   /*  cmd <n  */
                if((*iter).senderId == readId && (*iter).recverId == client->id && (*iter).sign == "USER_PIPE")
                {
                    close((*iter).pipefd[1]);
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }/* |0  cmd or | cmd */
                else if((*iter).count == 0 && readId == 0 && (*iter).clientId == client->id)
                {
                    close((*iter).pipefd[1]);   //close write to pipe
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }
            }
            iter++;
        }
    }
    else if(sign == "USER_PIPE")
    {
        if(UserPipeInError)
        {
            fd.in = -1;
            setIn = true;
        }
        if(UserPipeOutError)
        {
            fd.out = -1;
            setOut = true;
        }
        while(iter != pipe_vec.end())
        {
            if(!setIn)
            {   /*  cmd <n  */
                if((*iter).senderId == readId && (*iter).recverId == client->id && (*iter).sign == "USER_PIPE")
                {
                    close((*iter).pipefd[1]);
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }/* |0  cmd or | cmd */
                else if((*iter).count == 0 && readId == 0 && (*iter).clientId == client->id)
                {
                    close((*iter).pipefd[1]);   //close write to pipe
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }
            }
            if(!setOut)
            {
                if((*iter).senderId == client->id && (*iter).recverId == writeId && (*iter).sign == "USER_PIPE")
                {
                    fd.out = (*iter).pipefd[1];
                    setOut = true;
                }
            }
            iter++;
        }
    }
    else
    {
        if(UserPipeInError)
        {
            fd.in = -1;
            setIn = true;
        }
        while(iter != pipe_vec.end())
        {
            if(!setIn)
            {   /*  cmd <n  */
                if((*iter).senderId == readId && (*iter).recverId == client->id && (*iter).sign == "USER_PIPE")
                {
                    close((*iter).pipefd[1]);
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }/* |0  cmd or | cmd */
                else if((*iter).count == 0 && readId == 0 && (*iter).clientId == client->id)
                {
                    close((*iter).pipefd[1]);   //close write to pipe
                    fd.in = (*iter).pipefd[0];
                    setIn = true;
                }
            }
            iter++;
        }
    }
}

void SERVER::DoCmd(int clientFD)
{
    //test legal command
    if(!LegalCmd(cmd, arg_vec, path_vec))
    {
        string msg = "Unknown command: [" + cmd + "].\n";
        write(clientFD, msg.c_str(), msg.length());
        exit(1);
    }
    int devNullIn;
    int devNullOut;
    dup2(clientFD, 1);
    dup2(clientFD, 2);
    close(clientFD);
    //connect
    if(fd.in != 0)
    {
        if(fd.in == -1)     //user_pipe with no "In"
        {
            devNullIn = open("/dev/null", O_RDONLY);
            dup2(devNullIn, 0);     //redir "stdin" to "/dev/null" : EOF
        }
        else
            dup2(fd.in, 0);
    }
    if(fd.out != clientFD)
    {
        if(fd.out == -1)    //user_pipe with no "In"
        {
            devNullOut = open("/dev/null", O_WRONLY);
            dup2(devNullOut, 1);    //redir "stdout" to "/dev/null" : dump everything
        }
        else
            dup2(fd.out, 1);
    }
    if(fd.error != clientFD)
        dup2(fd.error, 2);
    //close
    if(fd.in != 0)
    {
        if(fd.in == -1) close(devNullIn);
        else close(fd.in);
    }
    if(fd.out != clientFD)
    {
        if(fd.in == -1) close(devNullOut);
        else close(fd.out);
    }
    if(fd.error != clientFD)
        close(fd.error);
    
    //do "exec"
    char **arg = SetArgv(cmd, arg_vec);
    for(int i=0 ; i<path_vec.size() ; i++)
    {
        string temp_path = path_vec[i] + "/" + cmd;
        execv(temp_path.c_str(), arg);
    }
    exit(1);
}

void SERVER::CreatePipe(int clientId, int senderId, int recverId)
{
    struct Pipe newPipe;

    int* pipefd = new int [2];
    pipe(pipefd);

    newPipe.pipefd = pipefd;
    newPipe.sign = sign;
    newPipe.count = count;
    newPipe.clientId = clientId;
    newPipe.senderId = senderId;
    newPipe.recverId = recverId;
    pipe_vec.push_back(newPipe);
}
void SERVER::ReduceCount_Ord(int clientId)
{
    for(int i=0; i<pipe_vec.size();i++)
    {
        if(pipe_vec[i].sign=="PIPE" && pipe_vec[i].clientId==clientId)
            pipe_vec[i].count--;
    }
}
void SERVER::ReduceCount_NUM_ERR(int clientId)
{
    for(int i=0; i<pipe_vec.size();i++)
    {
        if( (pipe_vec[i].sign=="NUM_PIPE" || pipe_vec[i].sign=="ERR_PIPE") && pipe_vec[i].clientId==clientId)
            pipe_vec[i].count--;
    }
}
void SERVER::ClosePipe(int clientId, int readId)
{
    for(int i=0; i<pipe_vec.size(); i++)
    {
        if(pipe_vec[i].count == 0 && pipe_vec[i].clientId==clientId)
        {
            close(pipe_vec[i].pipefd[0]);
            close(pipe_vec[i].pipefd[1]);
            delete [] pipe_vec[i].pipefd;
            pipe_vec.erase(pipe_vec.begin()+i);
            // break;      //to be safe, there could be both NUM_PIPE & ERR_PIPE pipe to same line
        }
        else if(pipe_vec[i].senderId == readId && pipe_vec[i].recverId == clientId && pipe_vec[i].sign == "USER_PIPE")  //??
        {
            close(pipe_vec[i].pipefd[0]);
            close(pipe_vec[i].pipefd[1]);
            delete [] pipe_vec[i].pipefd;
            pipe_vec.erase(pipe_vec.begin()+i);
            // break;      //to be safe, there could be both NUM_PIPE & ERR_PIPE pipe to same line
        }
    }
}
bool SERVER::PipeExist(int clientId)
{
    for(int i=0 ; i<pipe_vec.size() ; i++)
        if(pipe_vec[i].count == count && pipe_vec[i].sign == sign && pipe_vec[i].clientId == clientId) return true;

    return false;
}
bool SERVER::UserPipeExist(int senderId, int recverId)
{
    for(int i=0 ; i<pipe_vec.size() ; i++)
        if(pipe_vec[i].senderId == senderId && pipe_vec[i].recverId == recverId && pipe_vec[i].sign == "USER_PIPE") return true;

    return false;
}
bool SERVER::WaitChild()
{
    if(sign == "REDIR" || sign =="NONE") return true;

    return false;
}


int main(int argc, char* argv[]){
    int port = atoi(argv[1]);
    SERVER TIMMY;
    TIMMY.START_SERVER(port);

    return 0;
}

