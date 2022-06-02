#include "send_packet.h"
#include "common.h"

//Local save of clients, server should be the first "client
static struct nick_list* nicks = NULL;
//Addresses to the server and myself
static struct sockaddr_in server_addr, my_addr, tmp_addr;
//Some ints...
static int timeout, ret, loss_prob, port;
//Some fds
static int sock_fd, beat_timer_fd, time_out_fd, send_timer_fd;
//Some chars
char nick[NICK_SIZE], ip[INET_ADDRSTRLEN], res_buff[MSG_BUFF_SIZE], to_buff[MSG_BUFF_SIZE], from_buff[MSG_BUFF_SIZE];

//[DONE] Check format on input
void check_input (char* nick, char* address, int* port, int* timeout, int* loss_prob, char* argv[]) 
{
    //Copy into nick buffer and nullbyte in case of overflow
    if (check_ascii(argv[1]) == -1)
    {
        fprintf(stdout, "<nick> must only contain printable ASCII.\n");
        exit(EXIT_FAILURE); 
    }
    else if (strlen(argv[1]) > 19)
    {
        fprintf(stdout, "<nick> can't be longer than 20 characters.\n");
        exit(EXIT_FAILURE); 
    }
    strncpy(nick, argv[1], NICK_SIZE);
    nick[NICK_SIZE] = 0;

    //Remove spaces
    remove_spaces(nick);

    //Check if IPv4 valid address
    strncpy(address, argv[2], INET_ADDRSTRLEN);
    struct sockaddr_in sa;
    ret = inet_pton(AF_INET, address, &sa.sin_addr);
    if (ret != 1)
    {
        fprintf(stdout, "<address> must be an IPv4 address.\n");
        exit(EXIT_FAILURE); 
    }
    
    //Check if valid port
    *port = atoi(argv[3]);
    if (*port < 2048 || *port > 65535)
    {
        fprintf(stdout, "<port> must be an integer between 2048 - 65 535.\n");
        exit(EXIT_FAILURE); 
    }

    //Check if valid timeout. Might be beneficial to set an upper end to the value.
    *timeout = atoi(argv[4]);
    if (*timeout < 0)
    {
        fprintf(stdout, "<timeout> must be an integer equal to or higher than 0.\n");
        exit(EXIT_FAILURE); 
    }

    //Check if valid loss
    *loss_prob = atoi(argv[5]);
    if(*loss_prob < 0 || *loss_prob > 100)
    {
        fprintf(stdout, "<loss_probability> must be an integer between 0 - 100.\n");
        exit(EXIT_FAILURE); 
    }
}

//[DONE] Register myself on startup.
void register_nick (char* nick, int sock_fd)
{
    //Prepare select fd_set    
    fd_set select_fds;
    FD_ZERO(&select_fds);
    FD_SET(sock_fd, &select_fds);

    //Prepare and send msg
    sprintf(to_buff, reg, nicks -> cur_seq, nick);
    ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in));
    check_perror(ret, "sendto");

    //Prepare timer
    struct timeval timer = {timeout, 0};

    //Start select
    ret = select(FD_SETSIZE, &select_fds, NULL, NULL, &timer);
    check_perror(ret, "select");

    //Select timed out
    if(ret == 0) 
    { 
        free_nick_list(nicks);
        check_error(-1, "Registration timed out!\n");
    }

    //Socket ready to read
    if (FD_ISSET(sock_fd, &select_fds))
    {
        //Receive from server
        socklen_t server_addr_len = sizeof(struct sockaddr_in);
        ret = recvfrom(sock_fd, from_buff, MSG_BUFF_SIZE, 0, (struct sockaddr*)&server_addr, &server_addr_len); 
        check_perror(ret, "recvfrom");
        from_buff[ret] = 0;
        
        //Make sure the sequence to the server gets incremented
        nicks -> cur_seq++;
    }
}

//[DONE] Parses msg from stdin
void parse_stdin(char* pkt)
{
    
    //Check format on msg. Should be "@nick msg"
    if (pkt[0] != '@')
    { 
        char* keyword = strtok(pkt, " ");
        char* rest = strtok(NULL, "");

        if (keyword == NULL) keyword = "";
        if (rest == NULL) rest = "";

        struct nick_list* tmp = find_nick(rest, nicks);

        if (strcmp(keyword, "BLOCK") == 0 )
        {
            if (tmp != NULL)
            {
                tmp -> blocked_flag = 1;   
                fprintf(stdout, "\rYou blocked \"%s\".\n", tmp -> nick);
            }
            else
            {
                fprintf(stdout, "\rYou haven't spoken to \"%s\" this session.\n", rest);
            }
        }
        else if (strcmp(keyword, "UNBLOCK") == 0)
        {
            
            if (tmp != NULL)
            {
                tmp -> blocked_flag = 0;   
                fprintf(stdout, "\rYou unblocked \"%s\".\n", tmp -> nick);
            }
            else
            {
                fprintf(stdout, "\rYou haven't spoken to \"%s\" this session.\n", rest);
            }
        }
        else fprintf(stdout, "\rInvalid format. Usage \"@username msg\", \"BLOCK username\" or \"UNBLOCK username\".\n");
        
        return; 
    }

    //Get name of recipient and msg
    char* recipient = strtok(pkt, " ");
    char* msg = strtok(NULL, "");
    recipient++;

    //Incase msg is null (when user doesn't write a msg)
    if (msg == NULL) msg = "";    

    //Check if recipient is in cache
    struct nick_list* tmp = find_nick(recipient, nicks);

    //If recipient not in chache, check server
    if (tmp == NULL) 
    {
        //Prepares response
        sprintf(res_buff, look, nicks -> cur_seq, recipient);

        //Declare select fd_set
        fd_set select_fds;

        //While still retries left
        for (size_t i = 0; i < 3; i++)       
        {
            //Prepare select fd_set and timer
            FD_ZERO(&select_fds);
            FD_SET(sock_fd, &select_fds);
            struct timeval timer = {timeout, 0};

            //Send lookup
            ret = send_packet(sock_fd, res_buff, strlen(res_buff), 0, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in));
            check_perror(ret, "sendto");

            //Start select
            ret = select(FD_SETSIZE, &select_fds, NULL, NULL, &timer);
            check_perror(ret, "select");

            //Server timed out
            if(ret == 0)
            {
                if (i == 2)
                {
                    fprintf(stderr, "Lookup failed too many times! Server might be unavailable!\n");
                    fprintf(stdout, "NICK %s NOT REGISTERED\n", recipient);
                    return;
                } 
                else 
                {
                    fprintf(stderr, "Lookup timed out! Retrying!\n");
                    continue;
                }
            }
            
            //Server responded in time
            if (FD_ISSET(sock_fd, &select_fds))
            {   
                //Get response
                socklen_t server_addr_len = sizeof(struct sockaddr_in);
                ret = recvfrom(sock_fd, from_buff, MSG_BUFF_SIZE, 0, (struct sockaddr*)&server_addr, &server_addr_len); 
                check_perror(ret, "recvfrom");
                from_buff[ret] = 0;

                nicks -> cur_seq++;

                //Break (no need to continue)
                break;
            }
        }
        
        //Make tmp of buffer - strtok ruins pointer
        char tmp_packet_from_server[ret];
        strcpy(tmp_packet_from_server, from_buff);

        //Split response from server
        strtok(tmp_packet_from_server, " ");
        strtok(NULL, " ");
        char* not = strtok(NULL, " ");

        //If not found print error
        if (strcmp(not, "NOT") == 0)
        {
            fprintf(stdout, "NICK %s NOT REGISTERED\n", recipient);
            return;
        }

        //Create and put client in cache
        else
        {
            strtok(from_buff, " ");
            strtok(NULL, " ");
            strtok(NULL, " ");
            char* nick = strtok(NULL, " ");
            char* ip = strtok(NULL, " ");
            strtok(NULL, " ");
            char* port = strtok(NULL, " ");

            tmp = create_nick_list(nick, ip, atoi(port));
            //print_nick_list(tmp);
            nicks = add_to_nick_list(tmp, nicks);
        }
    } 
    
    //Add msg to clients msg queue
    if (tmp != NULL) tmp -> msgs = add_to_msg_list(create_msg_list(msg), tmp -> msgs);
}

//[DONE] Parses packets of type PKT from network
void parse_network_pkt(char* pkt, char* ip, int port)
{
    //Split pkg
    char* _pkt = strtok(pkt, " ");
    char* _seq_nr = strtok(NULL, " ");
    char* _from = strtok(NULL, " ");
    char* from_nick = strtok(NULL, " ");
    char* _to = strtok(NULL, " ");
    char* to_nick = strtok(NULL, " ");
    char* _msg = strtok(NULL, " ");
    char* msg = strtok(NULL, "");
    long seq_nr = strtol(_seq_nr, &_seq_nr, 10);

    //-----Check msg format------
    if (strcmp(to_nick, nick) != 0)
    {
        sprintf(to_buff, ack_wn, seq_nr);
        ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&tmp_addr, sizeof(struct sockaddr_in));
        check_perror(ret, "sendto");
        return;
    }
    else if (strcmp(_from, "FROM") != 0)
    {
        sprintf(to_buff, ack_wf, seq_nr);
        ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&tmp_addr, sizeof(struct sockaddr_in));
        check_perror(ret, "sendto");
        return;
    }
    else if (strcmp(_to, "TO") != 0)
    {
        sprintf(to_buff, ack_wf, seq_nr);
        ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&tmp_addr, sizeof(struct sockaddr_in));
        check_perror(ret, "sendto");
        return;
    }
    else if (strcmp(_msg, "MSG") != 0)
    {
        sprintf(to_buff, ack_wf, seq_nr);
        ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&tmp_addr, sizeof(struct sockaddr_in));
        check_perror(ret, "sendto");
        return;
    }
    else if (strcmp(_pkt, "PKT") != 0)
    {
        sprintf(to_buff, ack_wf, seq_nr);
        ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&tmp_addr, sizeof(struct sockaddr_in));
        check_perror(ret, "sendto");
        return;
    }
    //-----Check msg format------

    //Find client in cache
    struct nick_list* tmp = find_nick(from_nick, nicks);  

    //If not in cache, add
    if (tmp == NULL)
    {
        tmp = create_nick_list(from_nick, ip, port);
        tmp -> cur_seq = seq_nr;
        nicks = add_to_nick_list(tmp, nicks);
    }
    
    //Prepare address
    tmp_addr.sin_port = htons(port);
    ret = inet_pton(AF_INET, ip, &tmp_addr.sin_addr.s_addr);
    if (ret == 0) ret = -1;
    check_perror(ret, "inet_pton");

    //Prepare response
    sprintf(to_buff, ack_ok, seq_nr);

    //Send ack
    ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&tmp_addr, sizeof(struct sockaddr_in));
    check_perror(ret, "sendto");

    //Test?
    if (seq_nr == 0 && tmp -> cur_seq - 1 > 0) tmp -> cur_seq = seq_nr;

    //If pkt is not old print msg
    if (tmp -> cur_seq == seq_nr)
    {
        //Incase msg is null
        if (msg == NULL) msg = "";
        
        //Print msg
        if (tmp -> blocked_flag != 1) fprintf(stdout, "[%s]: %s\n", from_nick, msg);

        //Make sure that the next expected package is updated
        seq_nr++;
        tmp -> cur_seq = seq_nr; 
    } 
}

//[WIP] Parses packets of type ACK from network
void parse_network_ack(char* pkt, char* n_ip, int n_port)
{
    //Split packet
    strtok(pkt, " ");
    char* _seq_nr = strtok(NULL, " ");
    strtok(NULL, " ");

    //Find client (an ack has to have come from sombody ive sent a msg to... right?)
    struct nick_list* tmp = find_address(n_ip, n_port, nicks);
    
    if (tmp == NULL) return;

    if (strcmp(tmp -> nick, nick) == 0) tmp -> cur_seq--;

    //Remove from msg from cue and update counters
    if (tmp -> cur_seq == atoi(_seq_nr))
    {
        free_msg_list(pop_msg(&tmp -> msgs));
        tmp -> cur_seq++;
        tmp -> retries = 0;
        tmp -> updated_flag = 0;
        tmp -> in_flight_flag = 0;
    } 
}

//[Done] Signalhandler for SIGINT
void signal_handler()
{
    close(sock_fd);
    close(beat_timer_fd);
    close(send_timer_fd);
    close(time_out_fd);
    free_nick_list(nicks);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    //Check inputs and assign variables
    if(argc != 6) { fprintf(stdout, "Usage \"./upush_client <nick> <address> <port> <timeout> <loss_probability>\".\n"); return EXIT_FAILURE; }
    check_input(nick, ip, &port, &timeout, &loss_prob, argv);

    //Set loss_prob
    set_loss_probability(((double)loss_prob / (double)100));

    //Save server information locally in client list and create servers msg list (queries to server that need to be processed)
    char serverNick[2] = {17, 0};
    struct nick_list* server_nick = create_nick_list(serverNick, ip, port);
    server_nick -> msgs = NULL;
    nicks = add_to_nick_list(server_nick, nicks);

    //Create server and client address
    my_addr.sin_family = AF_INET;           
    my_addr.sin_port = htons(0);            
    my_addr.sin_addr.s_addr = INADDR_ANY;   
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    ret = inet_pton(AF_INET, ip, &server_addr.sin_addr.s_addr);
    if (ret == 0) ret = -1;
    check_perror(ret, "inet_pton");

    //Create socket
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    check_perror(sock_fd, "socket");

    //Register client
    register_nick(nick, sock_fd);

    //Sett and create, heartbeat timer, time out timer
    beat_timer_fd = timerfd_create(CLOCK_REALTIME,  0);
    struct itimerspec b_spec = { { 0, 0 }, { HEARTBEAT, 0 } };
    timerfd_settime(beat_timer_fd, 0, &b_spec, NULL);

    time_out_fd = timerfd_create(CLOCK_REALTIME,  0);
    struct itimerspec t_spec = { { 0, 0 }, { timeout, 0 } };
    timerfd_settime(time_out_fd, 0, &t_spec, NULL);

    //Add nullbyte to buffer, just in case
    to_buff[0] = 0;

    //Declare fd set
    fd_set select_fds;

    //Print welcome
    fprintf(stdout, "-=Welcome to UPush! Fastn't, securen't and reliablen't!=-\n You can now be found with \"%s\".\n", nick);

    //Prepare SIGINT catch
    signal(SIGINT, signal_handler);

    //Main loop
    while (strcmp(to_buff, "QUIT"))
    {
        //Checks if msgs can be sent
        for (struct nick_list* tmp_nicks = nicks; tmp_nicks != NULL; tmp_nicks = tmp_nicks -> next_nick)
        {
            if (tmp_nicks -> blocked_flag == 1) 
            {
                free_msg_list(pop_msg(&tmp_nicks -> msgs));
                continue;
            }

            if (tmp_nicks -> in_flight_flag == 0 && tmp_nicks -> msgs != NULL)
            {
                //Create address
                tmp_addr.sin_family = AF_INET;
                tmp_addr.sin_port = htons(tmp_nicks -> port);
                ret = inet_pton(AF_INET, tmp_nicks -> ip, &tmp_addr.sin_addr.s_addr);
                if (ret == 0) ret = -1;
                check_perror(ret, "inet_pton");
            
                //Create packet and change expected sequence
                sprintf(to_buff, txt_msg, tmp_nicks -> cur_seq, nick, tmp_nicks -> nick, (tmp_nicks -> msgs) -> msg);
                
                //Send packet
                ret = send_packet(sock_fd, to_buff, strlen(to_buff), 0, (struct sockaddr*)&tmp_addr, sizeof(struct sockaddr_in));
                check_perror(ret, "sendto");
            
                tmp_nicks -> last_seen = time(0);
                tmp_nicks -> in_flight_flag = 1;
            }
        }

        //Sett fd set
        FD_ZERO(&select_fds);
        FD_SET(STDIN_FILENO, &select_fds);
        FD_SET(sock_fd, &select_fds);
        FD_SET(beat_timer_fd, &select_fds);
        FD_SET(time_out_fd, &select_fds);

        //Select listener
        ret = select(FD_SETSIZE, &select_fds, NULL, NULL, NULL);
        check_perror(ret, "select");

        //[Done]Incoming from user
        if (FD_ISSET(STDIN_FILENO, &select_fds)) 
        {
            //Gets string
            get_string(to_buff, MSG_BUFF_SIZE);

            //Quit on keyword "QUIT"
            if(strcmp(to_buff, "QUIT") == 0)
            {
                fprintf(stdout, "Quitting!\n");
                break;
            }

            //Else parse
            parse_stdin(to_buff); 
        }
    
        //[DONE]Incoming from network
        if (FD_ISSET(sock_fd, &select_fds))
        {
            //Receieve from network
            socklen_t tmp_addr_len = sizeof(struct sockaddr_in);
            ret = recvfrom(sock_fd, from_buff, MSG_BUFF_SIZE, 0, (struct sockaddr*)&tmp_addr, &tmp_addr_len); 
            check_perror(ret, "recvfrom");
            from_buff[ret] = 0;

            //Parse packet
            if (from_buff[0] == 'P') parse_network_pkt(from_buff, inet_ntoa(tmp_addr.sin_addr), ntohs(tmp_addr.sin_port));
            else if (from_buff[0] == 'A') parse_network_ack(from_buff, inet_ntoa(tmp_addr.sin_addr), ntohs(tmp_addr.sin_port));  
        }

        //[WIP]Time out timer
        if (FD_ISSET(time_out_fd, &select_fds))
        {
            //For each client...
            for (struct nick_list* tmp_nicks = nicks; tmp_nicks != NULL; tmp_nicks = tmp_nicks -> next_nick)
            {
                //If not timed out, continue
                if (!((time(0) - tmp_nicks -> last_seen) > timeout && tmp_nicks -> in_flight_flag)) continue;
                
                tmp_nicks -> in_flight_flag = 0;  
                
                //First resend
                if (tmp_nicks -> retries == 0)
                {
                    fprintf(stderr, "Connection to %s timed out! Resending.\n", tmp_nicks -> nick); 
                    tmp_nicks -> retries++;
                    continue;
                }

                //Lookup after first resend
                if (tmp_nicks -> retries == 1)
                {
                    fprintf(stderr, "Connection to %s timed out! Looking up information.\n", tmp_nicks -> nick);
                
                    //Declare fd_set and timer for lookup
                    fd_set lookup_fds;
                    FD_ZERO(&lookup_fds);
                    FD_SET(sock_fd, &lookup_fds);
                    struct timeval timer = {timeout, 0};
                
                    //Send lookup
                    sprintf(res_buff, look, nicks -> cur_seq, tmp_nicks -> nick);
                    ret = send_packet(sock_fd, res_buff, strlen(res_buff), 0, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in));
                    check_perror(ret, "sendto");
                
                    //Start select
                    ret = select(FD_SETSIZE, &lookup_fds, NULL, NULL, &timer);
                    check_perror(ret, "select");
                
                    if(ret == 0)
                    {
                        fprintf(stderr, "Server failed to respond on lookup of \"%s\".\n", tmp_nicks -> nick);
                        tmp_nicks -> retries = 4;
                        continue;
                    }
                
                    //Server responded in time
                    if (FD_ISSET(sock_fd, &lookup_fds))
                    {   
                        //Get response
                        socklen_t server_addr_len = sizeof(struct sockaddr_in);
                        ret = recvfrom(sock_fd, from_buff, MSG_BUFF_SIZE, 0, (struct sockaddr*)&server_addr, &server_addr_len); 
                        check_perror(ret, "recvfrom");
                        from_buff[ret] = 0;

                        //Make tmp of buffer - strtok ruins pointer
                        char tmp_packet_from_server[ret];
                        strcpy(tmp_packet_from_server, from_buff);
                        strtok(tmp_packet_from_server, " ");
                        strtok(NULL, " ");
                        char* not = strtok(NULL, " ");
                
                        //If not found print error
                        if (strcmp(not, "NOT") == 0)
                        {
                            fprintf(stdout, "NICK %s NOT REGISTERED\n", tmp_nicks -> nick);
                            tmp_nicks -> retries = 4;
                            continue;
                        }
                
                        //Create and put client in cache
                        strtok(from_buff, " "); strtok(NULL, " "); strtok(NULL, " "); strtok(NULL, " ");
                        char* new_ip = strtok(NULL, " ");
                        strtok(NULL, " ");
                        char* new_port = strtok(NULL, " ");
                
                        if (tmp_nicks -> port == atoi(new_port) && strcmp(tmp_nicks -> ip, new_ip) == 0) 
                        {tmp_nicks -> retries = 4; continue;}
                        
                        strcpy(tmp_nicks -> ip, new_ip);
                        tmp_nicks -> port = atoi(new_port);
                        tmp_nicks -> updated_flag = 1;
                        tmp_nicks -> cur_seq = 0;
                        tmp_nicks -> in_flight_flag = 0;  
                        tmp_nicks -> retries++;
                    }
                }
                
                if ((tmp_nicks -> retries == 2 || tmp_nicks -> retries == 3) && tmp_nicks -> updated_flag == 1)
                {
                    tmp_nicks -> in_flight_flag = 0;
                    tmp_nicks -> retries++;
                    continue; 
                } 
            }
            
            struct nick_list* tmp_nicks = nicks;
            while (tmp_nicks != NULL)
            {
                if (tmp_nicks -> retries >= 4)
                { 
                    fprintf(stdout, "NICK %s UNREACHABLE.\n", tmp_nicks -> nick);
                    struct nick_list* remov = tmp_nicks;
                    tmp_nicks = tmp_nicks -> next_nick;
                    nicks = remove_nick(remov -> nick, nicks);

                    continue;
                }
                tmp_nicks = tmp_nicks -> next_nick;
            }
            
          
            timerfd_settime(time_out_fd, 0, &t_spec, NULL);
        }

        //[DONE] Heartbeat timer
        if (FD_ISSET(beat_timer_fd, &select_fds))
        {
            sprintf(res_buff, reg, nicks -> cur_seq, nick);
            nicks -> cur_seq++;
            ret = send_packet(sock_fd, res_buff, strlen(res_buff), 0, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in));
            check_perror(ret, "sendto");
            timerfd_settime(beat_timer_fd, 0, &b_spec, NULL);
        }
    }     

    close(sock_fd);
    close(beat_timer_fd);
    close(send_timer_fd);
    close(time_out_fd);
    free_nick_list(nicks);
    return 0;
}