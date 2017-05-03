/**
 * @minfanwa_assignment3
 * @author  Minfan Wang <minfanwa@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */

/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>//socket_in addr
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/unistd.h>//get hostname
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/queue.h>
#include <unistd.h>
#include <sys/select.h>

typedef enum {FALSE, TRUE} bool;
uint16_t CONTROL_PORT;
uint16_t ROUTER_PORT;
uint32_t ROUTER_IP_ADDR;
uint16_t DATA_PORT;
uint16_t update_interval, router_num;

#define AUTHOR_STATEMENT "I, minfanwa, have read and understood the course academic integrity policy."
#define CNTRL_HEADER_SIZE 8
#define CNTRL_RESP_HEADER_SIZE 8
#define ERROR(err_msg) {perror(err_msg); exit(EXIT_FAILURE);}
/* https://scaryreasoner.wordpress.com/2009/02/28/checking-sizeof-at-compile-time/ */
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)])) // Interesting stuff to read if you are interested to know how this works
#define PACKET_USING_STRUCT // Comment this out to use alternate packet crafting technique

#ifdef PACKET_USING_STRUCT
    struct __attribute__((__packed__)) CONTROL_HEADER
    {
        uint32_t dest_ip_addr;
        uint8_t control_code;
        uint8_t response_time;
        uint16_t payload_len;
    };

    struct __attribute__((__packed__)) CONTROL_RESPONSE_HEADER
    {
        uint32_t controller_ip_addr;
        uint8_t control_code;
        uint8_t response_code;
        uint16_t payload_len;
    };
#endif
struct routing_table_header
{
	uint16_t num_update_field;
	uint16_t source_router_port;
	uint32_t source_router_ip_addr;
}table_header;

struct rounting_table
{
	uint32_t router_ip_addr;
	uint16_t router_port;
	uint16_t data_port;
	uint16_t padding;
	uint16_t router_id;
	uint16_t cost;
	uint16_t next_hop;
	uint16_t isNeighbour;
}*table, *recv_table;

/* Linked List for active data connections */
struct DataConn
{
    int sockfd;
    LIST_ENTRY(DataConn) next;
}*dataconnection, *data_conn_temp;
LIST_HEAD(DataConnsHead, DataConn) data_conn_list;

/*****************************************************************************************/
void author_response(int sock_index);
void main_loop();
void init();
int create_control_sock();
int new_control_conn(int sock_index);
void remove_control_conn(int sock_index);
bool isControl(int sock_index);
bool control_recv_hook(int sock_index);
char* create_response_header(int sock_index, uint8_t control_code, uint8_t response_code, uint16_t payload_len);
ssize_t recvALL(int sock_index, char *buffer, ssize_t nbytes);
ssize_t sendALL(int sock_index, char *buffer, ssize_t nbytes);
void init_response(int sock_index, char* cntrl_payload);
void init_routing_table(char* cntrl_payload);
int create_router_sock(uint16_t ROUTER_PORT);
void new_router_conn(int sock_index);
void send_router_table(int sock_index);
// char* making_routing_update_pack();

/*****************************************************************************************/
int main(int argc, char **argv)
{
	/*Start Here*/
	sscanf(argv[1], "%" SCNu16, &CONTROL_PORT);
	init();
	return 0;
}
/*********************************** Author Response *************************************/
void author_response(int sock_index)
{
	uint16_t payload_len, response_len;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;

	payload_len = sizeof(AUTHOR_STATEMENT)-1; // Discount the NULL chararcter
	cntrl_response_payload = (char *) malloc(payload_len);
	memcpy(cntrl_response_payload, AUTHOR_STATEMENT, payload_len);

	cntrl_response_header = create_response_header(sock_index, 0, 0, payload_len);

	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	/* Copy Header */
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	free(cntrl_response_header);
	/* Copy Payload */
	memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);
	free(cntrl_response_payload);

	sendALL(sock_index, cntrl_response, response_len);

	free(cntrl_response);
}

/*********************************************************************************/

fd_set master_list, watch_list;
int head_fd;
int control_socket, router_socket, data_socket;

void main_loop()
{
    int selret, sock_index, fdaccept;

    while(TRUE){
        watch_list = master_list;
        selret = select(head_fd+1, &watch_list, NULL, NULL, NULL);

        if(selret < 0)
            ERROR("select failed.");

        /* Loop through file descriptors to check which ones are ready */
        for(sock_index=0; sock_index<=head_fd; sock_index+=1){

            if(FD_ISSET(sock_index, &watch_list)){

                if(sock_index == control_socket){
                    fdaccept = new_control_conn(sock_index);

                    /* Add to watched socket list */
                    FD_SET(fdaccept, &master_list);
                    if(fdaccept > head_fd) head_fd = fdaccept;
                }

                /* router_socket */
                else if(sock_index == router_socket){
                    //call handler that will call recvfrom() .....
                    new_router_conn(sock_index);
                }

                /* data_socket */
                else if(sock_index == data_socket){
                    //new_data_conn(sock_index);
                }

                /* Existing connection */
                else{
                    if(isControl(sock_index)){
                        if(!control_recv_hook(sock_index)) FD_CLR(sock_index, &master_list);
                    }
                    //else if isData(sock_index);
                    else ERROR("Unknown socket index");
                }
            }
        }
    }
}
/**************************************************************************************/

void init()
{
    control_socket = create_control_sock();
    //router_socket and data_socket will be initialized after INIT from controller
    FD_ZERO(&master_list);
    FD_ZERO(&watch_list);
    /* Register the control socket */
    FD_SET(control_socket, &master_list);
    head_fd = control_socket;

    main_loop();
}
/**************************************************************************************/
#ifndef PACKET_USING_STRUCT
    #define CNTRL_CONTROL_CODE_OFFSET 0x04
    #define CNTRL_PAYLOAD_LEN_OFFSET 0x06
#endif

/* Linked List for active control connections */
struct ControlConn
{
    int sockfd;
    LIST_ENTRY(ControlConn) next;
}*connection, *conn_temp;
LIST_HEAD(ControlConnsHead, ControlConn) control_conn_list;

int create_control_sock()
{
    int sock;
    struct sockaddr_in control_addr;
    socklen_t addrlen = sizeof(control_addr);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
        ERROR("socket() failed");

    /* Make socket re-usable */
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int)) < 0)
        ERROR("setsockopt() failed");

    bzero(&control_addr, sizeof(control_addr));

    control_addr.sin_family = AF_INET;
    control_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    control_addr.sin_port = htons(CONTROL_PORT);

    if(bind(sock, (struct sockaddr *)&control_addr, sizeof(control_addr)) < 0)
        ERROR("bind() failed");

    if(listen(sock, 5) < 0)
        ERROR("listen() failed");

    LIST_INIT(&control_conn_list);

    return sock;
}

int new_control_conn(int sock_index)
{
    int fdaccept, caddr_len;
    struct sockaddr_in remote_controller_addr;

    caddr_len = sizeof(remote_controller_addr);
    fdaccept = accept(sock_index, (struct sockaddr *)&remote_controller_addr, &caddr_len);
    if(fdaccept < 0)
        ERROR("accept() failed");

    /* Insert into list of active control connections */
    connection = malloc(sizeof(struct ControlConn));
    connection->sockfd = fdaccept;
    LIST_INSERT_HEAD(&control_conn_list, connection, next);

    return fdaccept;
}

void remove_control_conn(int sock_index)
{
    LIST_FOREACH(connection, &control_conn_list, next) {
        if(connection->sockfd == sock_index) LIST_REMOVE(connection, next); // this may be unsafe?
        free(connection);
    }

    close(sock_index);
}

bool isControl(int sock_index)
{
    LIST_FOREACH(connection, &control_conn_list, next)
        if(connection->sockfd == sock_index) return TRUE;

    return FALSE;
}

bool control_recv_hook(int sock_index)
{
    char *cntrl_header, *cntrl_payload;
    uint8_t control_code;
    uint16_t payload_len;

    /* Get control header */
    cntrl_header = (char *) malloc(sizeof(char)*CNTRL_HEADER_SIZE);
    bzero(cntrl_header, CNTRL_HEADER_SIZE);

    if(recvALL(sock_index, cntrl_header, CNTRL_HEADER_SIZE) < 0){
        remove_control_conn(sock_index);
        free(cntrl_header);
        return FALSE;
    }

    /* Get control code and payload length from the header */
    #ifdef PACKET_USING_STRUCT
        /** ASSERT(sizeof(struct CONTROL_HEADER) == 8) 
          * This is not really necessary with the __packed__ directive supplied during declaration (see control_header_lib.h).
          * If this fails, comment #define PACKET_USING_STRUCT in control_header_lib.h
          */
        BUILD_BUG_ON(sizeof(struct CONTROL_HEADER) != CNTRL_HEADER_SIZE); // This will FAIL during compilation itself; See comment above.

        struct CONTROL_HEADER *header = (struct CONTROL_HEADER *) cntrl_header;
        control_code = header->control_code;
        payload_len = ntohs(header->payload_len);
    #endif
    #ifndef PACKET_USING_STRUCT
        memcpy(&control_code, cntrl_header+CNTRL_CONTROL_CODE_OFFSET, sizeof(control_code));
        memcpy(&payload_len, cntrl_header+CNTRL_PAYLOAD_LEN_OFFSET, sizeof(payload_len));
        payload_len = ntohs(payload_len);
    #endif

    free(cntrl_header);

    /* Get control payload */
    if(payload_len != 0){
        cntrl_payload = (char *) malloc(sizeof(char)*payload_len);
        bzero(cntrl_payload, payload_len);

        if(recvALL(sock_index, cntrl_payload, payload_len) < 0){
            remove_control_conn(sock_index);
            free(cntrl_payload);
            return FALSE;
        }
    }

    /* Triage on control_code */
    switch(control_code){
        case 0: author_response(sock_index);
                break;

        case 1: //make response to the controller, only response code, no payload
        		init_response(sock_index, cntrl_payload);
        		//write the data into local routing table
        		init_routing_table(cntrl_payload);
        		//initialize router and data socket
        		create_router_sock(ROUTER_PORT);
        		send_router_table(sock_index);

        		// create_data_sock(DATA_PORT);
                break;

        // case 2: routing_table_response(sock_index);
        // 		break;

        // case 3: update();
        // 		break;

        // case 4: crash();
        // 		break;

        // case 5: sendfile();
        // 		break;

        // case 6: sendfile_stats();
        // 		break;

        // case 7: last_data_packet();
        // 		break;

        // case 8: penultimate_data_packet();
        // 		break;

    }

    if(payload_len != 0) free(cntrl_payload);
    return TRUE;
}
/**************************************************************************************/
#ifndef PACKET_USING_STRUCT
    #define CNTRL_RESP_CONTROL_CODE_OFFSET 0x04
    #define CNTRL_RESP_RESPONSE_CODE_OFFSET 0x05
    #define CNTRL_RESP_PAYLOAD_LEN_OFFSET 0x06
#endif

char* create_response_header(int sock_index, uint8_t control_code, uint8_t response_code, uint16_t payload_len)
{
    char *buffer;
    #ifdef PACKET_USING_STRUCT
        /** ASSERT(sizeof(struct CONTROL_RESPONSE_HEADER) == 8) 
          * This is not really necessary with the __packed__ directive supplied during declaration (see control_header_lib.h).
          * If this fails, comment #define PACKET_USING_STRUCT in control_header_lib.h
          */
        BUILD_BUG_ON(sizeof(struct CONTROL_RESPONSE_HEADER) != CNTRL_RESP_HEADER_SIZE); // This will FAIL during compilation itself; See comment above.

        struct CONTROL_RESPONSE_HEADER *cntrl_resp_header;
    #endif
    #ifndef PACKET_USING_STRUCT
        char *cntrl_resp_header;
    #endif

    struct sockaddr_in addr;
    socklen_t addr_size;

    buffer = (char *) malloc(sizeof(char)*CNTRL_RESP_HEADER_SIZE);
    #ifdef PACKET_USING_STRUCT
        cntrl_resp_header = (struct CONTROL_RESPONSE_HEADER *) buffer;
    #endif
    #ifndef PACKET_USING_STRUCT
        cntrl_resp_header = buffer;
    #endif

    addr_size = sizeof(struct sockaddr_in);
    getpeername(sock_index, (struct sockaddr *)&addr, &addr_size);

    #ifdef PACKET_USING_STRUCT
        /* Controller IP Address */
        memcpy(&(cntrl_resp_header->controller_ip_addr), &(addr.sin_addr), sizeof(struct in_addr));
        /* Control Code */
        cntrl_resp_header->control_code = control_code;
        /* Response Code */
        cntrl_resp_header->response_code = response_code;
        /* Payload Length */
        cntrl_resp_header->payload_len = htons(payload_len);
    #endif

    #ifndef PACKET_USING_STRUCT
        /* Controller IP Address */
        memcpy(cntrl_resp_header, &(addr.sin_addr), sizeof(struct in_addr));
        /* Control Code */
        memcpy(cntrl_resp_header+CNTRL_RESP_CONTROL_CODE_OFFSET, &control_code, sizeof(control_code));
        /* Response Code */
        memcpy(cntrl_resp_header+CNTRL_RESP_RESPONSE_CODE_OFFSET, &response_code, sizeof(response_code));
        /* Payload Length */
        payload_len = htons(payload_len);
        memcpy(cntrl_resp_header+CNTRL_RESP_PAYLOAD_LEN_OFFSET, &payload_len, sizeof(payload_len));
    #endif

    return buffer;
}
/******************************************************************************************/

ssize_t recvALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = recv(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += recv(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

ssize_t sendALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = send(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += send(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

/********************************** Init Response ****************************************/

void init_response(int sock_index, char* cntrl_payload)
{
	//match all the receive data into the response header and create local routing initial table
	//pass all the data into local routing table
	uint16_t payload_len, response_len;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;
	//create header
	payload_len = 0;
	cntrl_response_header = create_response_header(sock_index, 1, 0, payload_len);

	response_len = CNTRL_RESP_HEADER_SIZE + payload_len;
	cntrl_response = (char *) malloc (response_len);
	/*copy header*/
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	free(cntrl_response_header);
	
	sendALL(sock_index, cntrl_response, response_len);
	free(cntrl_response);
}
/************************************* Init Table *********************************************************/
#define CONTROL_UPDATE_INTERVAL_OFFSET 0x02
#define ROUTER_ID_OFFSET 0x04
#define ROUTER_PORT_OFFSET 0x06
#define DATA_PORT_OFFSET 0x08
#define COST_OFFSET 0x0A
#define ROUTER_IP_OFFSET 0x0C

void init_routing_table(char* cntrl_payload)
{
	int i;
	memcpy(&table_header.num_update_field, cntrl_payload, sizeof(table_header.num_update_field));
	table_header.num_update_field = ntohs(table_header.num_update_field);
	memcpy(&update_interval, cntrl_payload + CONTROL_UPDATE_INTERVAL_OFFSET, sizeof(update_interval));
	update_interval = ntohs(update_interval);
	printf("cntrl_payload: %x\n", *cntrl_payload);
	
	router_num = table_header.num_update_field;
	struct rounting_table *table = (struct rounting_table*)malloc(sizeof(struct rounting_table));
	printf("router_num: %d, payload router_num: %d \n", router_num, table_header.num_update_field);
	printf("INIT routing table content from controller !\n");
	for( i = 0; i < router_num; i++){
		memcpy(&table[i].router_ip_addr, cntrl_payload + ROUTER_IP_OFFSET + i*12, sizeof(table[i].router_ip_addr));
		table[i].router_ip_addr = ntohl(table[i].router_ip_addr);
		memcpy(&table[i].router_port, cntrl_payload + ROUTER_PORT_OFFSET + i*12, sizeof(table[i].router_port));
		table[i].router_port = ntohs(table[i].router_port);
		table[i].padding = 0x00;
		memcpy(&table[i].router_id, cntrl_payload + ROUTER_ID_OFFSET+ i*12, sizeof(table[i].router_id));
		table[i].router_id = ntohs(table[i].router_id);
		memcpy(&table[i].cost, cntrl_payload + COST_OFFSET + i*12, sizeof(table[i].cost));
		table[i].cost = ntohs(table[i].cost);
		memcpy(&table[i].data_port, cntrl_payload + DATA_PORT_OFFSET + i*12, sizeof(table[i].data_port));
		table[i].data_port = ntohs(table[i].data_port);
		
		printf("%d : IP address: %02x, ROUTER_PORT: %d, DATA_PORT: %d, Padding: %d, ROUTER_ID: %d, COST: %d\n", 
			i+1, table[i].router_ip_addr, table[i].router_port, table[i].data_port,
			table[i].padding, table[i].router_id, table[i].cost);
		if(table[i].cost > 0 && table[i].cost < 65535){
			table[i].isNeighbour = 1;
		}
	}
	//get the router port of the host
	int j; 
	for(j = 0; j < router_num; j ++){
		if(table[j].cost == 0){
			ROUTER_PORT = table[j].router_port;
			DATA_PORT = table[j].data_port;
			ROUTER_IP_ADDR = table[j].router_ip_addr;
		}
	}
	printf("ROUTER_PORT: %d, ROUTER_IP_ADDR: %02x\n", ROUTER_PORT, ROUTER_IP_ADDR);
	
}
#define ROUTING_TABLE_HEADER_SIZE 8

#define SOURCE_ROUTER_PORT_OFFSET 0x02
#define SOURCE_ROUTER_IP_OFFSET 0x04

#define PORT_OFFSET 0x04
#define PADDING_OFFSET 0x06
#define ID_OFFSET 0x08
#define ROUTER_COST_OFFSET 0x0A

void send_router_table(int sock_index)
{
	char *routing_table_header, *routing_table_payload, *rounting_table_pack;

	/*make the header*/
	uint16_t router_num_cpy, router_port_cpy;
	uint32_t router_ip_cpy;
	routing_table_header = (char *) malloc(ROUTING_TABLE_HEADER_SIZE);
	router_num_cpy = htons(router_num);
	memcpy(routing_table_header, &router_num_cpy, sizeof(router_num_cpy));
	router_port_cpy = htons(ROUTER_PORT);
	memcpy(routing_table_header+SOURCE_ROUTER_PORT_OFFSET, &router_port_cpy, sizeof(router_port_cpy));
	router_ip_cpy = htonl(ROUTER_IP_ADDR);
	memcpy(routing_table_header+SOURCE_ROUTER_IP_OFFSET, &router_ip_cpy, sizeof(router_ip_cpy));
	printf("\nROUTING TABLE \n");
	printf("ROUTER_NUM: %d, SRC_PORT: %d, SRC_IP: %02x\n", router_num, ROUTER_PORT, ROUTER_IP_ADDR);
	
	/*make the payload*/
	uint16_t routing_table_payload_len;
	routing_table_payload_len = 12 * router_num;
	uint16_t table_router_port_cpy, table_padding_cpy, table_id_cpy, table_cost_cpy;
	uint32_t table_ip_cpy;
	routing_table_payload = (char *) malloc(routing_table_payload_len);

	int i;
	for(i = 0; i <router_num; i ++){
		table_ip_cpy = htonl(table[i].router_ip_addr);
		memcpy(routing_table_payload + i*12, &table_ip_cpy, sizeof(table_ip_cpy));
		table_router_port_cpy = htons(table[i].router_port);
		memcpy(routing_table_payload + i*12+PORT_OFFSET, &table_router_port_cpy, sizeof(table_router_port_cpy));
		table_padding_cpy = htons(table[i].padding);
		memcpy(routing_table_payload + i*12+PADDING_OFFSET, &table_padding_cpy, sizeof(table_padding_cpy));
		table_id_cpy = htons(table[i].router_id);
		memcpy(routing_table_payload + i*12+ID_OFFSET, &table_id_cpy , sizeof(table_id_cpy));
		table_cost_cpy = htons(table[i].cost);
		memcpy(routing_table_payload + i*12+COST_OFFSET, &table_cost_cpy, sizeof(table_cost_cpy));
		printf("IP: %02x, PORT: %d, PADDING: %d, ID: %d, COST: %d\n", table[i].router_ip_addr, table[i].router_port,
			table[i].padding, table[i].router_id, table[i].cost);
	}

	int routing_table_pack_len; 
	routing_table_pack_len =  ROUTING_TABLE_HEADER_SIZE + routing_table_payload_len;
	
	/*pack the table*/
	rounting_table_pack = (char *) malloc(sizeof(routing_table_pack_len));
	memcpy(rounting_table_pack, routing_table_header, sizeof(routing_table_header));
	free(routing_table_header);
	memcpy(rounting_table_pack+ROUTING_TABLE_HEADER_SIZE, routing_table_payload, sizeof(routing_table_payload_len));
	free(routing_table_payload);

	int send, raddr_len, k;
	struct sockaddr_in remote_router_addr;
	// initial table
	for( k = 0; k < router_num; k++){
		if(table[k].isNeighbour == 1){
			raddr_len = sizeof(struct sockaddr_in);
			getpeername(sock_index, (struct sockaddr *)&remote_router_addr, &raddr_len);
			send = sendto(sock_index, rounting_table_pack, routing_table_pack_len, 0, 
				(struct sockaddr *)&remote_router_addr, raddr_len);
			if(send < 0){
				ERROR("sendto() failed");
			}
		}
	}

	free(rounting_table_pack);

}

int create_router_sock(uint16_t router_port)
{
	//UDP
	int sock;
	struct sockaddr_in router_ip_addr;
	socklen_t addrlen = sizeof(router_ip_addr);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0)
		ERROR("socket() failed");
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (int[]) {1}, sizeof(int))<0)
		ERROR("setsockopt() failed");

	bzero(&router_ip_addr, sizeof(router_ip_addr));

	router_ip_addr.sin_family = AF_INET;
	router_ip_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	router_ip_addr.sin_port = htons(router_port); //router port is in the routing table
	//port is the router port of the router itself

	if(bind(sock, (struct sockaddr *)&router_ip_addr, sizeof(router_ip_addr))<0)
		ERROR("bind() failed");

	return sock;
}

void new_router_conn(int sock_index)//recvfrom
{
	int recv, raddr_len, byte_count;
    struct sockaddr_in remote_router_addr;
    char *buf;

    raddr_len = sizeof(remote_router_addr);
    byte_count = recvfrom(sock_index, buf, sizeof(buf), 0 ,
    	(struct sockaddr *)&remote_router_addr, &raddr_len);
    if(byte_count< 0)
        ERROR("recvfrom() failed");   
    //deal with the data received to implement update routing table
    //get the information of the next

    // struct rounting_table *recv_table = (struct rounting_table*)malloc(sizeof(struct rounting_table));
    
}
// void send_router_table(int sock_index)
// {
// 	int send, raddr_len;
// 	struct sockaddr_in remote_router_addr;
// 	int i;
// 	char *buf;

// 	buf = making_routing_update_pack();

// 	for( int i = 0; i < router_num; i++){
// 		if(table[i].cost>0 || table[i].cost < 65535){
// 			raddr_len = sizeof(struct sockaddr_in);
// 			getpeername(sock_index, (struct sockaddr *)&remote_router_addr, &raddr_len);
// 			send = sendto(sock_index, buf, sizeof(buf), 0, 
// 				(struct sockaddr *)&remote_router_addr, raddr_len);
// 			if(send < 0){
// 				ERROR("sendto() failed");
// 			}
// 		}
// 	}
// }
