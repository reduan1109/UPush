#include "send_packet.h"
#include "common.h"

static struct nick_list* nicks = NULL;
static struct sockaddr_in _addr;
static int heart_beat_timer, sock_fd;
static int port, loss_prob, ret;
static char msg_buff[MSG_BUFF_SIZE];

//[DONE] Check format on input
void check_input (int* port, int* loss_prob, char* argv[])
{
    //Check if valid port
    *port = atoi(argv[1]);
    if (*port < 2048 || *port > 65535)
    {
        fprintf(stderr, "<port> must be a number between 2048 - 65 535.\n");
        exit(EXIT_FAILURE); 
    }

    //Check if valid loss
    //FUTURE: Check if loss_probability is number (atoi always gives an int)
    *loss_prob = atoi(argv[2]);
    if(*loss_prob < 0 || *loss_prob > 100)
    {
        fprintf(stderr, "<loss_probability> must be a number between 0 - 100.\n");
        exit(EXIT_FAILURE); 
    }
}

//[DONE] Parses network packet. Prob better to just tak in the socket address struct
void parse_packet(char* pkt, int source_port, char* source_ip, char* response_buffer)
{
    //Tmp struct 
    struct nick_list* tmp = NULL;

    //Split pkt
    long seq_nr = 0;
    char* msg_first_type = strtok(pkt, " ");
    char* msg_sequence_nr = strtok(NULL, " ");
    char* msg_second_type = strtok(NULL, " ");
    char* msg_nick_name = strtok(NULL, " ");

    if (msg_sequence_nr != NULL) seq_nr = strtol(msg_sequence_nr, &msg_sequence_nr, 10);
    
    //Format check
    if (strcmp(msg_first_type, "PKT") != 0 || check_ascii(msg_nick_name) == -1)
        sprintf(response_buffer, ack_wf, seq_nr);
    
    //If reg, create person and add to cache
    else if (strcmp(msg_second_type, "REG") == 0)
    {
        sprintf(response_buffer, ack_ok, seq_nr);

        //FUTURE: New alloc on heartbeat is a waste. Find person, if null make new.
        struct nick_list* tmp = create_nick_list(msg_nick_name, source_ip, source_port);
        nicks = add_to_nick_list(tmp, nicks);
        find_nick(msg_nick_name, nicks) -> last_seen = time(0);
    }
    //If lookup, find person and pepare response if availiable
    else if (strcmp(msg_second_type, "LOOKUP") == 0) 
    {
        tmp = find_nick(msg_nick_name, nicks);
        if (tmp == NULL) sprintf(response_buffer, look_fail, seq_nr);
        else if ((time(0) - tmp -> last_seen) > (HEARTBEAT*3)) sprintf(response_buffer, look_fail, seq_nr);
        else sprintf(response_buffer, look_succ, seq_nr, tmp -> nick, tmp -> ip, tmp -> port);
    }
}

//[Done] Signalhandler for SIGINT
void signal_handler()
{
    fprintf(stderr, "\nQuitting!\n");
    close(sock_fd);
    close(heart_beat_timer);
    nicks = free_nick_list(nicks);
    exit(EXIT_SUCCESS);
}

//[DONE] Main 
int main(int argc, char *argv[])
{
    //Check input
    if(argc != 3) { fprintf(stderr, "Usage \"./upush_server <port> <loss_probability>\".\n"); return EXIT_FAILURE; }       
    check_input(&port, &loss_prob, argv);

    //Sett loss
    set_loss_probability(((double)loss_prob / (double)100));

    //Create socket
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    check_perror(sock_fd, "socket");

    //Bind port
    _addr.sin_family = AF_INET;        
    _addr.sin_port = htons(port);      
    _addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(sock_fd, (struct sockaddr*)&_addr, sizeof(struct sockaddr_in));
    check_perror(ret, "bind");

    //Declare fd set and heart beat timer
    fd_set select_fds;
    heart_beat_timer = timerfd_create(CLOCK_REALTIME,  0);
    struct itimerspec b_spec = { { 0, 0 }, { HEARTBEAT, 0 } };
    timerfd_settime(heart_beat_timer, 0, &b_spec, NULL);
    
    //Prepare SIGINT catch
    signal(SIGINT, signal_handler);
    
    //Main loop. Is 1 for ease
    while(1)
    {
        //Prepare select fds
        FD_ZERO(&select_fds);
        FD_SET(sock_fd, &select_fds);
        FD_SET(STDIN_FILENO, &select_fds);
        FD_SET(heart_beat_timer, &select_fds);

        //Select listener
        ret = select(FD_SETSIZE, &select_fds, NULL, NULL, NULL);
        check_perror(ret, "select");

        //[Done] Incoming from admin
        if (FD_ISSET(STDIN_FILENO, &select_fds)) 
        {
            //Gets string
            get_string(msg_buff, MSG_BUFF_SIZE);

            //Quit on keyword "QUIT"
            if (strcmp(msg_buff, "QUIT") == 0) { fprintf(stderr, "Quitting!\n"); break; }
            else if (strcmp(msg_buff, "LS") == 0) print_nick_list(nicks);
        }

        //[DONE] Incoming from network
        if (FD_ISSET(sock_fd, &select_fds))
        {
            //Receive from network
            socklen_t _addr_len = sizeof(struct sockaddr_in);
            ret = recvfrom(sock_fd, msg_buff, MSG_BUFF_SIZE, 0, (struct sockaddr*)&_addr, &_addr_len); 
            check_perror(ret, "recvfrom");
            msg_buff[ret] = 0;

            fprintf(stderr, "From network: %s\n", msg_buff);
            
            //Parse pkt
            parse_packet(msg_buff, ntohs(_addr.sin_port), inet_ntoa(_addr.sin_addr), msg_buff);

            //Send response
            ret = send_packet(sock_fd, msg_buff, strlen(msg_buff), 0, (struct sockaddr*)&_addr, sizeof(struct sockaddr_in));
            check_perror(ret, "sendto");
        }

        //[DONE] Active check
        if (FD_ISSET(heart_beat_timer, &select_fds))
        {
            //Loops over all the client and figures out when the client was last seen.
            //If more than 3*HEARTBEAT, remove. For loop causes a concurrency error
            struct nick_list* tmp_nicks = nicks;
            while(tmp_nicks != NULL)
            {
                if ((time(0) - tmp_nicks -> last_seen) >= (HEARTBEAT*3) ) 
                {
                    fprintf(stderr, "User %s, has been inactive for %lds. Removing!\n", tmp_nicks -> nick, (time(0) - tmp_nicks -> last_seen));
                    
                    struct nick_list* remove = tmp_nicks;
                    tmp_nicks = tmp_nicks -> next_nick;
                    nicks = remove_nick(remove -> nick, nicks);
                    
                    continue;
                }
                tmp_nicks = tmp_nicks -> next_nick;
            }  
            timerfd_settime(heart_beat_timer, 0, &b_spec, NULL);
        }
    }

    //Clean up
    close(sock_fd);
    close(heart_beat_timer);
    nicks = free_nick_list(nicks);
    return EXIT_SUCCESS;
}