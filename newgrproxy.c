#include <signal.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKLOG 10
#define MAXLINE 65535

//These are defines for network "commands"
#define MY_JOIN 0xBE			//our client joined the server
#define PL_MOVE 0xC4			//player movement
#define PL_DEATH 0xCD			//player death
#define PL_MISS 0xCB			//player missed shot
#define PL_GREN 0xC9			//player grenade sot
#define PL_HIT	0xCA			//player hit shot
#define SV_MESG 0xBA			//a server network message
													//subcommands for this below

//These are defines for the above network message cmd
//they define what the network message means
#define M_MY_TM_CHG 0xFF	//team change for proxy client player
#define M_TM_CHG 0x00			//team change for others
#define M_EDT_REQ 0x2C		//client send server edit request
#define M_EDT_FIN 0x2E		//server finish edit
#define M_EDT_DNY 0x30		//server deny edit request
#define M_SV_CHAT 0x5B		//server informational chat
#define M_PL_CHAT 0x5C		//player chat
#define M_PL_SPWN 0x46		//player spawns
#define M_PL_UNHIDE 0x57	//cmd to unhide bots - for coop
#define M_PL_HIDE 0x58		//cmd to hide bots - for coop
#define M_FOG_CHG 0x83		//fog change
#define M_PL_JOIN 0x89		//client server join
#define M_PL_QUIT 0x8C		//client quit server

//define the maximum clients/players in a game
//these are array max values
#define MAXPLAYERS 300
#define MAXCLIENTS 300

//one of these is filled out for every client
//that joins the game
typedef struct client_st {
	char client_id;			//the client number assigned by server
	char team_id;				//team the client joined
} gr_client;

//upon spawning in a game, this structure is
//filled out for a player character
typedef struct player_st {
	char player_id;			//player number a client spawned as
	char client_id;			//client number - relates to gr_client
	short x;						//player x position
	short y;						//player y position
	short z;						//player z position
	short angle1;				//angle from PL_MOVE command
	short angle2;				//angle from PL_MOVE command
	float a1;						//angle from PL_MISS
	float a2;						//angle from PL_MISS
	char is_bot;				//set if this player is an AI bot
	char visible;				//set if the player is not hidden(M_PL_HIDE)
} player;

player me;							//set to be the proxy client's player struct
player hack_id;					//set to be the hacked client's player struct
int self_destruct = 0;	//self destruct on/off(1/0)
gr_client me_client;		//client structure for proxy client
int fog=0;							//set to 1 when we get our initial fog setting
int nobots=1;						//bots in game? 1=no
int autoaim=1;					//autoaim on/off
int autonade=1;					//autonade on/off
int showall=0;					//show enemies on map on/off
short s_time;						//gametime needed for some network commands
#define OUTFILE					//outfile stuff is for file/stdout output
#ifdef OUTFILE
FILE *output = 0;
#elif
FILE *output = stdout; //output to stdout by default
#endif

player players[MAXPLAYERS];			//player array
gr_client clients[MAXCLIENTS];	//client array
int numplayers = 0;							//number of players
int numclients = 0;							//number of clients
int sv_sock, cl_sock;						//server and client sockets
char origfog[47];

//update a client structure with new data
void updateclient(gr_client temp);

//add a new client to the array
void addclient(gr_client temp);

//remove a client from the array
void remclient(gr_client temp);

//remove a player from the array
void remplayer(player temp);

//change a player's visibilty
void visplayer(player temp);

//update a player's strucutre
void updateplayer(player temp);

//add a new player to the array
void addplayer(player temp);

//send a chat message through GR... as server or any player
int GR_printf(char **to, char id, char type,char *format, ...);

//chat command interpretation
void docommand(char mesg[], char **tocl_end, char **tosv_end);

//find a target for redirecting grenades
int findtarget_gren(player hack_me);

//find a target for redirecting a missed bullet
int findtarget(player hack_me);

//find the team of a client by client number
int findteam(int client_id);

//listen for connection from client
int cl_connect(int port);

//connect to the game server
int sv_connect(char *server);

//start up the proxy's network handling/command interpretation
void doproxy(int clsock, int svsock);

//interperet a single client message
int cl_message(char buffer[], char **end, char tocl[], char **tocl_end, char tosv[], char **to_end);

//interperet a single server message
int sv_message(char buffer[], char **end, char tocl[], char **tocl_end, char tosv[], char **to_end);

//turn fog off on the client side
void GR_fogoff(char **tocl_end);

//show all enemies on the command map
void GR_showall(int showall, char **tocl_end);

//force the game to start
void GR_gamestart(char **tosv_end);

//kick a player
void GR_kick(char id, char **tosv_end);

//teleport a player a certain distance in a certain direction
void GR_teleport_dist(char **tocl_end, char **tosv_end, char axis, int dist, player *hack_me);

//delete this
void GR_teleport_plr(char **tocl_end, char **tosv_end);

//suicide script... suicides the player with the id passed as target
void self_destroy(char **tosv_end, char **tocl_end, int target);

//move proxy client's angle to coincide with a hacked shot
void changeangle(char **tocl_end, char **tosv_end, player *pl, short angle1, short angle2);

void teamkill(char **tosv_end, int tker);

void fire(char **tosv_end);

void serveredit(char client_id, char **tocl_end, char **tosv_end);

void editfin(char **tocl_end, char **tosv_end);

//used for bsearch function...
int cmpmove(const void *move1, const void *move2)
{
	player * mover1, *mover2;
	mover1 = (player *) move1; 
	mover2 = (player *) move2;
	if (mover1->player_id > mover2->player_id)
		return 1;
	if (mover1->player_id < mover2->player_id)
		return -1;
	return 0;
}

//using movement angles to target/fire
float anglec2f(unsigned char angle, int trig) {
	if(trig)return sin((angle/(double)255)*360);
	return cos((angle/(double)255)*360);
}

int main(int argc, char *argv[]) 
{	
	pid_t pid;

	//TODO: CHECK ARGUMENTS FOR CONSISTENCY
	if (argc != 3) {
		fprintf(stderr, "usage: %s hostname:port port\n", argv[0]);
		fprintf(stderr, "\thostname:port specifies where to connect\n");
		fprintf(stderr, "\tport specifies a port to listen on\n");
		exit(1);
	}

#ifdef OUTFILE
	output = fopen("/home/riptide/src/newgrproxy/proxylog.txt","w");
	if(output < 0)
		return 0;
#endif

	//wait for client connection(while loop)
	cl_sock = cl_connect(atoi(argv[2]));
		
	//connect to the server
	sv_sock = sv_connect(argv[1]);

	//-1 for no client to hack
	hack_id.client_id = -1;
	doproxy(cl_sock, sv_sock);

	return 1;
}

//this function should only return when a connection is established
int cl_connect(int port) 
{
	int sockfd, new_fd;				/* listen on sock_fd, new connection on new_fd */
	struct sockaddr_in my_addr;	/* my address information */
	struct sockaddr_in their_addr;	/* connector's address information */
	unsigned int sin_size;										//size of sockaddr
	int yes = 1;										//boolean true for later on

	//create a socket for the client connection
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}
	//set our socket options
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		perror("setsockopt");
		exit(1);		
	}

	my_addr.sin_family = AF_INET;	/* host byte order */
	my_addr.sin_port = htons(port);	/* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY;	/* automatically fill with my IP */
	memset(&(my_addr.sin_zero), '\0', 8);	/* zero the rest of the struct */

	//bind our socket to the port we're using
	if (bind(sockfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr)) == -1) {
		perror("bind");
		exit(1);
	}
	//listen for a connection on our socket(blocking call)
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}
	//accept our connection on a new socket
	sin_size = sizeof(struct sockaddr_in);
	if ((new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size)) == -1) {
		perror("error accepting connection");
	}

	//finish up here.
	fprintf(output, "server: got connection from %s\n", inet_ntoa(their_addr.sin_addr));
	close(sockfd);							/* connected... stop listening */
	return new_fd;							/* return connected socket */
}

/* Connect to the Ghost Recon server
	 This function is run when we have connected with a client */ 
int sv_connect(char *server) 
{
	char *portptr;									//char pointer for dest. port
	int sockfd, port;								//socket and port variables
	struct hostent *he;							//host struct for gethostbyname
	struct sockaddr_in their_addr;	// connector's address information 
	
	portptr = strrchr(server, ':');	//get our port
	*portptr = 0;
	port = atoi(portptr+1);
	if ((he = gethostbyname(server)) == NULL) {	// get the host IP
		perror("gethostbyname");
		exit(1);
	}
	//create a socket for the connection
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}
	their_addr.sin_family = AF_INET;	// host byte order 
	their_addr.sin_port = htons(port);	// short, network byte order 
	their_addr.sin_addr = *((struct in_addr *) he->h_addr);
	memset(&(their_addr.sin_zero), '\0', 8);	// zero the rest of the struct 
	//connect the socket to the server
	if (connect(sockfd, (struct sockaddr *) &their_addr, sizeof(struct sockaddr)) == -1) {
		perror("error connecting");
		exit(1);
	}
	fprintf(output, "Connected to %s using socket %d\n", server, sockfd);
	return sockfd;
}

//run the proxy... network management
void doproxy(int clsock, int svsock) {
	int maxsock, sockflags, closeconn;
	ssize_t nread, nwritten;
	fd_set rset, wset;
	char tosv[MAXLINE], frsv[MAXLINE], tocl[MAXLINE], frcl[MAXLINE];
	char *tosv_end, *frsv_end, *tocl_end, *frcl_end;

	//set both sockets to nonblocking
	sockflags = fcntl(clsock, F_GETFL, 0);
	fcntl(clsock, F_SETFL, sockflags | O_NONBLOCK);
	
	sockflags = fcntl(svsock, F_GETFL, 0);
	fcntl(svsock, F_SETFL, sockflags | O_NONBLOCK);

	//initialize buffers
	tosv_end = tosv;
	frsv_end = frsv;
	tocl_end = tocl;
	frcl_end = frcl;
	closeconn = 0;

	//find out which socket has the greater value descriptor
	if(clsock > svsock)
		maxsock = clsock+1;
	else
		maxsock = svsock+1;

	//loop until we need to close the connection
	for ( ;!closeconn ; ) {
		//get a socket set for a call to select
		FD_ZERO(&rset);
		FD_ZERO(&wset);
		if(closeconn == 0 && frcl_end < &frcl[MAXLINE])
			FD_SET(clsock, &rset);
		if(closeconn == 0 && frsv_end < &frsv[MAXLINE])
			FD_SET(svsock, &rset);
		if(tocl_end != tocl)
			FD_SET(clsock, &wset);
		if(tosv_end != tosv)
			FD_SET(svsock, &wset);

		//find out which sockets we need to work with
		select(maxsock, &rset, &wset, NULL, NULL);

		if(FD_ISSET(clsock, &rset)) {
			//read what we can.
			if((nread = read(clsock, frcl_end, &frcl[MAXLINE] - frcl)) < 0) {
				if(errno != EWOULDBLOCK)
					fprintf(stderr, "read error on client socket\n");
			} else if(nread == 0) {
			//if there's nothing to read, but the socket was set, the socket is closed
				fprintf(stderr, "EOF on client socket\n");
				closeconn = 1;
		//		if(tosv_end == tosv)
					close(svsock);
			} else {
			//We've got data, get the proxy to process it here
				//fprintf(stderr, "read %d bytes from client socket\n", (int)nread);
				frcl_end += nread;
				//DO PROCESSING OF INCOMMING DATA HERE
				while(cl_message(frcl, &frcl_end, tocl, &tocl_end, tosv, &tosv_end));
				//fprintf(stderr, "processed %d bytes from client socket\n", (int)nread);
				FD_SET(svsock, &wset);
			}
		}
		if(FD_ISSET(svsock, &rset)) {
			//read what we can.
			if((nread = read(svsock, frsv_end, &frsv[MAXLINE] - frsv)) < 0) {
				if(errno != EWOULDBLOCK)
					fprintf(stderr, "read error on server socket\n");
			} else if(nread == 0) {
			//if there's nothing to read, but the socket was set, the socket is closed
				fprintf(stderr, "EOF on server socket\n");
				closeconn=1;
		//		if(tocl_end == tocl)
					close(clsock);
			} else {
			//We've got data, get the proxy to process it here
				//fprintf(stderr, "read %d bytes from server socket\n", (int)nread);
				frsv_end += nread;
				//DO PROCESSING OF INCOMMING DATA HERE
				while(sv_message(frsv, &frsv_end, tocl, &tocl_end, tosv, &tosv_end));
				//fprintf(stderr, "processed %d bytes from server socket\n", (int)nread);
				FD_SET(clsock, &wset);
			}
		}
		//fprintf(stderr, "We have %d bytes to write to client\n", (tocl_end-tocl));
		//got something to write to outbound?  do it here
		if(FD_ISSET(clsock, &wset) && ((nread = tocl_end-tocl) > 0)) {
			if((nwritten = write(clsock, tocl, nread)) < 0) { 
				if(errno != EWOULDBLOCK)
					fprintf(stderr, "Write error to client socket\n");
			} else {
				//no error on write, so update the buffer
				//fprintf(stderr, "wrote %d bytes to client socket\n", nwritten);
				tocl_end -= nwritten;
				memcpy(tocl, (tocl+nwritten), (tocl_end-tocl)); 
			}
			//fprintf(stderr, "wrote %d bytes to client socket\n", nwritten);
		}
		//fprintf(stderr, "We have %d bytes to write to server\n", (tosv_end-tosv));
		//outbound to server... write if we have data and room to write
		if(FD_ISSET(svsock, &wset) && ((nread = tosv_end-tosv) > 0)) {
			if((nwritten = write(svsock, tosv, nread)) < 0) { 
				if(errno != EWOULDBLOCK)
					fprintf(stderr, "Write error to server socket\n");
			} else {
				//no error on write, so update the buffer
				//fprintf(stderr, "wrote %d bytes to server socket\n", nwritten);
				tosv_end -= nwritten;
				memcpy(tosv, (tosv+nwritten), (tosv_end-tosv)); 
			}
			//fprintf(stderr, "wrote %d bytes to server socket\n", nwritten);
		}
	}
}

int cl_message(char buffer[], char **end, char tocl[], char **tocl_end, char tosv[], char **tosv_end) {
	short mesglen, z;
	char mesg[MAXLINE];
	char hitbullet[29] =
		"\x42\x1A\x00\xCA\x0B\x16\x00\x04\x00\x00\x00\x00\x94\x52\x15\x41\x8D\x7F\x28\x3F\x03\x00\x00\x00\x07\x8A\x84\x83\x00";
	//CA = hit bullet. 
	int target;
	player key, *find;
	int magnitude, numbytes, i, placement, numhits;
	float angle1, angle2;
	double arctan, mag;
	
	//seems like this is some sort of keepalive... pass it straight through
	if(buffer[0] == 0x20) {
		memcpy(buffer, &buffer[1], (*end-buffer));
		*end -= 1;
		return 1;
	}
	
	//GR messages should always start with 0x42
	if(buffer[0] != 0x42)
		return 0;

	//only 2 bytes means incomplete message; wait for another pass
	if(*end <= &buffer[2])
		return 0;
	
	//get the length of the next message.
	//if we don't have a complete message, wait for another pass
	memcpy(&mesglen, &buffer[1], sizeof(short));
	if((mesglen+3) > (*end-buffer))
		return 0;
	
	//copy the current message out into a temp buffer
	memcpy(mesg, buffer, (int)(mesglen+3));

	//update the main buffer... remove the message we're processing
	if((*end-buffer) > (mesglen+3)) {
		memcpy(buffer, (buffer+mesglen+3), ((*end-buffer)-(mesglen+3)));
		*end -= (mesglen+3);
	} else {
		*end = buffer;
	}
	//Do message categorization/modification after this point
	//if there's something to send, add it to the tocl or tosv buffer
	
	//fprintf(stderr, "Processing %d bytes of client message\n", (mesglen+3));
	
	switch(mesg[3]) {
		case (char)PL_MOVE:
			//get new player position and update array
			me.player_id = mesg[11];
			memcpy(&me.x, &mesg[12], sizeof(short));
			memcpy(&me.y, &mesg[14], sizeof(short));
			memcpy(&me.z, &mesg[16], sizeof(short));
			memcpy(&s_time, &mesg[7], sizeof(short));
//			fprintf(stderr, "I'm moving player id: %d x: %d, y: %d, z: %d\n", me.player_id, me.x, me.y, me.z);
			me.angle1 = (unsigned char) mesg[24];
			me.angle2 = (unsigned char) mesg[25];
			me.a1 = anglec2f(mesg[24], 0);
			me.a2 = anglec2f(mesg[24], 1);
			updateplayer(me);
			memcpy(*tosv_end, mesg, (mesglen+3));
			*tosv_end += (mesglen+3);
			break;
		case (char)PL_MISS:
			//if we have autoaim on, reaim the shot
			if(!autoaim) {
			//autoaim is off... copy directly to output buffer
				memcpy(*tosv_end, mesg, (mesglen+3));
				*tosv_end += (mesglen+3);
				break;
			}
			//prepare information for a new hacked hit
			hitbullet[7] = me.player_id;
			//update my angle based off of the missed shot
			memcpy(&me.a1, &mesg[18], sizeof(float));
			memcpy(&me.a2, &mesg[22], sizeof(float));
			//find a target to shoot at
			target = findtarget(me);
			if (target < 0)  //oops, no target found... a real miss
				break;
			//set up to hit our target
			key.player_id = hitbullet[20] = (char) target;
			//get info of the target so we can aim at him
			find = bsearch(&key, players, numplayers, sizeof(player), cmpmove);
			if (find == NULL){	//didn't find our target... real miss
				memcpy(*tosv_end, mesg, (mesglen+3));
				*tosv_end += (mesglen+3);
				break;
			}
			//distance to our target... trig ;)
			magnitude = sqrt(pow((find->x - me.x), 2) + pow((find->y - me.y), 2));
			//horizontal angle to our target(sin for 1 cos for 2) + 10 on 2(GR magic)
			angle1 = (find->x - me.x) / (float) magnitude;
			angle2 = 10 + ((find->y - me.y) / (float) magnitude);
			memcpy(&hitbullet[12], &angle1, sizeof(float));
			memcpy(&hitbullet[16], &angle2, sizeof(float));
			
			//make sure we have a target before we send
			if (hitbullet[20] != -1) {
				//move our angles so it looks like we're hitting for real in replays
				changeangle(end, tosv_end, &me, angle1, angle2);
				//copy the hit into output buffer to send later
				memcpy(*tosv_end, hitbullet, 29);
				*tosv_end += 29;
				i=0;
				//target =(int) hitbullet[20];
				//while(players[i].player_id != target) i++;
				//mag = sqrt((pow((players[i].x - me.x), 2) + pow((players[i].y - me.y), 2)));
				//arctan = ((players[i].y - me.y) / (float) mag) / ((players[i].x - me.x) / (float) mag);
				//memcpy(&placement, &mesg[24], sizeof(int));
				//fprintf(stderr, "%f, %f, %d\n", mag, arctan, placement);
				//GR_printf(cl_sock, 0, M_SV_CHAT, "total sent hits: %d", ++numhits);
			} else {
				//changeangle(end, tosv_end, find, angle1, angle2);
				//me.a1 = find->a1;
				//me.a2 = find->a2;
				//no hit.. copy the miss to output
				memcpy(*tosv_end, mesg, (mesglen+3));
				*tosv_end += (mesglen+3);
			}
			break;
		case (char)PL_GREN:
//			\x42\x19\x00\xC9\x0B\x15\x00\x01\x34\x01\x59\x06\x24\x02\x5D\x00\x99\x47\x10\x41\xF2\x67\x30\xBE\x00\x00\x00\x00
			if(!autonade) { //don't hack a nade if we're not in autonade mode
				memcpy(*tosv_end, mesg, (mesglen+3));
				*tosv_end += (mesglen+3);
				break;
			}
			//pick our target
			target = findtarget_gren(me);
			if (target < 0)
				break;

			//find the target player
			key.player_id = (char) target;
			//fprintf(output, "target: %d\n", target);
			find = bsearch(&key, players, numplayers, sizeof(player), cmpmove);
			if (find == NULL){  //couldn't find the target, shoot grenade as normal
				memcpy(*tosv_end, mesg, (mesglen+3));
				*tosv_end += (mesglen+3);
				fprintf(output, "Grenade, no target!\n");
				break;
			}
			//calculate distance/angles... fix this
			//right now this is calculated in a straight line as it would be for
			//a bullet
			magnitude = sqrt(pow((find->x - me.x), 2) + pow((find->y - me.y), 2));
			angle1 = (find->x - me.x) / (float) magnitude;
			angle2 = 10 + ((find->y - me.y) / (float) magnitude);
			z = find->z;
			z+=10;  //this is done so the nade doesn't fall through the map
			memcpy(&mesg[10], &find->x, sizeof(short));
			memcpy(&mesg[12], &find->y, sizeof(short));
			memcpy(&mesg[14], &z, sizeof(short));
			memcpy(&mesg[16], &angle1, sizeof(float));
			memcpy(&mesg[20], &angle2, sizeof(float));
			memcpy(*tosv_end, mesg, (mesglen+3));
			*tosv_end += (mesglen+3);
			break;
		case (char)SV_MESG:
			if(/*mesg[7] == M_EDT_REQ ||*/ mesg[7] == M_EDT_FIN) {
					editfin(tocl_end, tosv_end);
				//mesg[15] = (char)0;
//				mesg[19] = (char)0;
//				numbytes = 1;
//				memcpy(&mesg[15], &numbytes, sizeof(int));
//				memcpy(*tosv_end, mesg, (mesglen+3));
//				*tosv_end += (mesglen+3);
//				memcpy(&s_time, &mesg[9], sizeof(short));
//				serveredit(mesg[12], tocl_end, tosv_end);
			}
			//we've got a chat message... process it with docommand
			//or send it on if it doesn't begin with '/'

			//fprintf(output, "me: %d Talker: %d char: %c\n", me_client.client_id, mesg[15], mesg[25]);
			if(mesg[7] == (char)M_PL_CHAT && mesg[15] == me_client.client_id && mesg[25] == '/')
			{
				fprintf(output, "message processing\n");
				docommand(mesg, tocl_end, tosv_end);
				numbytes = 0;
			} else {
				memcpy(*tosv_end, mesg, (mesglen+3));
				*tosv_end += (mesglen+3);
			}
			break;
		case (char)PL_HIT: //pass hits straight through as with any other traffic
		default:					//hits are there just in case I want to add stuff later
			memcpy(*tosv_end, mesg, (mesglen+3));
			*tosv_end += (mesglen+3);
			break;
	}
	
	return 1;
}

int sv_message(char buffer[], char **end, char tocl[], char **tocl_end, char tosv[], char **to_end) {
	short mesglen, z;
	char mesg[MAXLINE];
	player temp;
	gr_client tempclient;
	int i;
	char hitbullet[29] =
		"\x42\x1A\x00\xCA\x0B\x16\x00\x04\x00\x00\x00\x00\x94\x52\x15\x41\x8D\x7F\x28\x3F\x03\x00\x00\x00\x07\x8A\x84\x83\x00";
	int target;
	player key, *find;
	int magnitude, numbytes;
	float angle1, angle2;
	double arctan, mag;

	//keepalive byte... send it on
	if(buffer[0] == 0x20) {
		memcpy(buffer, &buffer[1], (*end-buffer));
		*end -= 1;
		return 1;
	}
	
	//valid messages begin with 0x42
	if(buffer[0] != 0x42)
		return 0;

	//too short, wait for more data
	if(*end <= &buffer[2])
		return 0;
	
	//get the message length
	//wait for more data if we don't have a full message in queue
	memcpy(&mesglen, &buffer[1], sizeof(short));
	if((mesglen+3) > (*end-buffer))
		return 0;
	
	//copy message to temp buffer and adjust main buffer
	memcpy(mesg, buffer, (int)(mesglen+3));
	if((*end-buffer) > (mesglen+3)) {
		memcpy(buffer, (buffer+mesglen+3), ((*end-buffer)-(mesglen+3)));
		*end -= (mesglen+3);
	} else {
		*end = buffer;
	}
	
	//Do message checking/modification here
	//if there's something to send, add it to the tocl or tosv buffer
	
//	fprintf(stderr, "Processing %d bytes of server message\n", (mesglen+3));
	//just to make sure...
	temp.x = temp.y = temp.z = 0;
	temp.angle1 = temp.angle2 = 0;
	
	switch(mesg[3]) {
		case (char)SV_MESG:
				switch(mesg[7]) {
					case (char)M_EDT_DNY: //edit server attempt failed
						memcpy(&s_time, &mesg[9], sizeof(short));
						serveredit(mesg[12], tocl_end, to_end);
						mesglen = 0;
						break;
					case (char)M_MY_TM_CHG:
						break;
					case (char)M_TM_CHG:	//update a client's team
						if(mesg[8] != 0x01)	//eliminate noise(ok, so I read a char
							break;						//when I should have read a short)
						tempclient.client_id = mesg[15];
						tempclient.team_id = mesg[18];
						//fprintf(output, "my id: %d	changer id: %d\n", me_client.client_id, tempclient.client_id);
						//be sure to keep me_client up-to-date
						if(tempclient.client_id == me_client.client_id) {
							me_client.team_id = tempclient.team_id;
							//fprintf(output, "my team: %d\n", me_client.team_id);
						}
						//update the client array
						updateclient(tempclient);
						//fprintf(output, "Client: %d changed team to %d\n", mesg[15], mesg[18]);
						break;
					case (char)M_PL_SPWN:					//player spawn processing
						if(mesg[28] != (char)0x4E)	//nade/sensor and other stuff spawns
							break;
						temp.player_id = mesg[19];
						temp.client_id = mesg[25];
						temp.is_bot = !mesg[(mesglen+2)];
						temp.visible = 1;						//bots/players are visible by default
						if(temp.is_bot)							//if we get a bot, update our global var
							nobots =0;								//used in targeting routine
// This used to be needed for some reason to keep the client_id
// correct.  I dont' think it's needed anymore and it's bad code.
//						if(temp.player_id == me.player_id) {
//							me.client_id = temp.client_id;
//							fprintf(output, "I am player id %d\n", me.client_id);
//						}
						//if we're hacking a player, and this player is the one then
						//we need to update the hack structure
						if(hack_id.client_id > -1 && temp.client_id == hack_id.client_id)
							hack_id.player_id = temp.player_id;
						//add the new spawn to our array
						addplayer(temp);
						//keep a loop on suicides if we have that enabled
						if(self_destruct)
							self_destroy(to_end, tocl_end, -1);
						//new spawn, show everyone on the command map again(if enabled)
						GR_showall(showall, tocl_end);
						break;
					case (char)M_PL_UNHIDE:		//hide/unhide mainly for bot players
						temp.player_id = mesg[15];
						temp.visible = 1; 			//these are for targeting... don't target
						visplayer(temp);				//hidden bots(crashes server in old versions)
						break;
					case (char)M_PL_HIDE:
						if(mesg[8] != (char)0x00) {
							temp.player_id = mesg[15];
							temp.visible = 0;
							visplayer(temp);
						}
						break;
					case (char)M_SV_CHAT:			//catch game launch to reset game data
						/* game launch... clear out previous players */
						if(strstr(&mesg[25], "ancement:") != 0) {
							fog = 0;
							numplayers = 0;
							showall = 0;
							nobots=1;
							fprintf(output, "We are go for launch!\n");
						}
						if(strstr(&mesg[25], "Preparing to launch:") != 0) {
							fog = 0;
							numplayers = 0;
							nobots=1;
							fprintf(output, "We are go for launch!\n");
						}
						break;
					case (char)M_FOG_CHG:
						if(mesg[8] != 01)
							break;
						if(fog)
							return 1;
						memcpy(origfog, mesg, (mesglen+3));
					case (char)M_PL_JOIN:		//new player join... add to array
						if(mesg[12] != 0x05)	//filter out noise
							break;
						tempclient.client_id = mesg[15];
						tempclient.team_id = mesg[81];
						addclient(tempclient);
						//fprintf(output, "Client: %d joined... team: %d\n", mesg[15], mesg[81]);
						break;
					case (char)M_PL_QUIT:		//player quit... remove from arrays
						tempclient.client_id = mesg[15];
						remclient(tempclient);

						//if we were hacking this guy, stop
						if(hack_id.client_id == tempclient.client_id)
							hack_id.client_id = -1;
						break;
					default:
						break;
				}
			break;
		case (char)MY_JOIN:		//get my client id on join
			fprintf(output, "Setting client id to: %d\n", mesg[5]);
			me_client.client_id = mesg[5];
			me_client.team_id = 0;
			break;
		case (char)PL_MOVE:		//get my info when I move around
			temp.player_id = mesg[11];
			memcpy(&temp.x, &mesg[12], sizeof(short));
			memcpy(&temp.y, &mesg[14], sizeof(short));
			memcpy(&temp.z, &mesg[16], sizeof(short));
			temp.angle1 = (short) mesg[24];
			temp.angle2 = (short) mesg[25];

			//convert movement angles to aim angles
			temp.a1 = anglec2f(mesg[24], 0);
			temp.a2 = anglec2f(mesg[24], 1);
			if(hack_id.client_id > -1 && temp.player_id == hack_id.player_id)
			{
				hack_id.x = temp.x;
				hack_id.y = temp.y;
				hack_id.z = temp.z;
				hack_id.angle1 = temp.angle1;
				hack_id.angle2 = temp.angle2;
				hack_id.a1 = temp.a1;
				hack_id.a2 = temp.a2;
			}
			updateplayer(temp);
			break;
		case (char)PL_DEATH:	//player died, update arrays
			//fprintf(output, "player %d died\n", mesg[7]);
			temp.player_id = mesg[7];
			//kind of a hack... reversing this part was odd
			if(mesg[12] == (char)0x03 || mesg[12] == (char)0x25)
			{
				remplayer(temp);
				self_destroy(to_end, tocl_end, -1);
			}
			break;
		//change this to something else that works...
		//case (char)PL_MISS:
		//kind of a hack... this isn't really a miss... just a shot
		//most likely I won't care if another player I'm hacking is
		//caught anyway...
		case (char)0xC7:
			fprintf(output, "Entering hacked player miss code\n");
			//if(hack_id.client_id < 0 || hack_id.player_id != mesg[11]) {
			//	fprintf(output, "Error 1\n");
			//	break;
			//}

			//make sure I'm hacking a player and this is him
			if(hack_id.client_id < 0 || hack_id.player_id != mesg[7]) {
				fprintf(output, "Error 1\n");
				break;
			}
			//find a target to shoot at
			hitbullet[7] = hack_id.player_id;
			target = findtarget(hack_id);
			if (target < 0) {
				fprintf(output, "Error 2\n");
				break;
			}
			//get info for our target
			key.player_id = hitbullet[20] = (char) target;
			find = bsearch(&key, players, numplayers, sizeof(player), cmpmove);
			if (find == NULL){
				fprintf(output, "Error 3\n");
				break;
			}
			//calculate angle info for our hacked shot
			fprintf(output, "calculating angles\n");
			magnitude = sqrt(pow((find->x - hack_id.x), 2) + pow((find->y - hack_id.y), 2));
			angle1 = (find->x - hack_id.x) / (float) magnitude;
			angle2 = 10 + ((find->y - hack_id.y) / (float) magnitude);
			memcpy(&hitbullet[12], &angle1, sizeof(float));
			memcpy(&hitbullet[16], &angle2, sizeof(float));
			//send it
			memcpy(*to_end, hitbullet, 29);
			*to_end += 29;
			i=0;
			//target =(int) hitbullet[20];
			//while(players[i].player_id != target) i++;
			//mag = sqrt((pow((players[i].x - hack_id.x), 2) + pow((players[i].y - hack_id.y), 2)));
			//arctan = ((players[i].y - hack_id.y) / (float) mag) / ((players[i].x - hack_id.x) / (float) mag);
			//memcpy(&placement, &mesg[24], sizeof(int));
			//fprintf(stderr, "%f, %f, %d\n", mag, arctan, placement);
			//GR_printf(cl_sock, 0, M_SV_CHAT, "total sent hits: %d", ++numhits);
			break;
		case (char)PL_GREN:
//			\x42\x19\x00\xC9\x0B\x15\x00\x01\x34\x01\x59\x06\x24\x02\x5D\x00\x99\x47\x10\x41\xF2\x67\x30\xBE\x00\x00\x00\x00
//		make sure I'm hacking a player and this is him
			if(hack_id.client_id < 0 || hack_id.player_id != mesg[7]) {
				break;
			}
			//find a target to hit
			target = findtarget_gren(hack_id);
			if (target < 0)
				break;
			//get information about the target
			key.player_id = (char) target;
			//fprintf(output, "target: %d\n", target);
			find = bsearch(&key, players, numplayers, sizeof(player), cmpmove);
			if (find == NULL) {
				break;
			}
			//calculate our angles and stuff... needs fixing
			magnitude = sqrt(pow((find->x - hack_id.x), 2) + pow((find->y - hack_id.y), 2));
			angle1 = (find->x - hack_id.x) / (float) magnitude;
			angle2 = 10 + ((find->y - hack_id.y) / (float) magnitude);
			z = find->z;
			z+=10;			//so the nade doesn't go through the map
			memcpy(&mesg[10], &find->x, sizeof(short));
			memcpy(&mesg[12], &find->y, sizeof(short));
			memcpy(&mesg[14], &z, sizeof(short));
			memcpy(&mesg[16], &angle1, sizeof(float));
			memcpy(&mesg[20], &angle2, sizeof(float));
			memcpy(*to_end, mesg, (mesglen+3));
			*to_end += (mesglen+3);
			break;
		default:
			break;
	}
	//copy normal message into buffer to the client
	if(mesglen) {
		memcpy(*tocl_end, mesg, (mesglen+3));
		*tocl_end += (mesglen+3);
	}
	
	return 1;
}

int findteam(int client_id)
{
	int j, id=0, thisteam=-1;

	for (j = 0;j < numclients; j++) {
		if(clients[j].client_id == client_id)
			thisteam = clients[j].team_id;
		id+=clients[j].team_id;
	}

	if(id == 0 && nobots)
		return -2;
	return thisteam;
}

int findtarget(player hack_me)
{
	int i, j;
	double arctan, smallest = 360, myarctan, deg, mydeg, amag, magnitude, mindist = -1;
	int aimat = -1, aimat2=-1;
	int myteam;

	//GR oddness... adjust angles
	if (hack_me.a1 > 8)
		hack_me.a1 -= 10;
	if (hack_me.a2 > 8)
		hack_me.a2 -= 10;

	myteam = findteam(hack_me.client_id);
	if(hack_me.client_id != hack_id.client_id) {
		//fprintf(stderr, "firing with my team\n");
		myteam = me_client.team_id;
	}
	
	//calculate degrees for where we're actually facing
	myarctan = hack_me.a2 / (double)hack_me.a1;
	mydeg = fabs((atan(myarctan) * 360) / (2 * M_PI));
	
	//loop through players and find enemy we're closest to aiming at
	for (i = 0; i < numplayers; i++) {
		if(numplayers == 2 && players[i].player_id != hack_me.player_id)
			return players[i].player_id;
		if(!players[i].visible) 	//player isn't visible, skip
			continue;
		if(!players[i].is_bot) {	//only check team if he's not a bot
			j = findteam(players[i].client_id);
			if(j == -1)							//-1 means no team was found
				continue;
			if(myteam == j) {
			//fprintf(output, "Friendly - Player: %d's team: %d\n", players[i].player_id, j);
				continue;
			}
			//fprintf(output, "After checking client's team\n");
			//fprintf(output, "Player: %d's team: %d\n", players[i].mover_id, j);
		}

		//calculate angle to target(relative to world)
		magnitude = sqrt((pow((players[i].x - hack_me.x), 2) + pow((players[i].y - hack_me.y), 2)));

		arctan = ((players[i].y - hack_me.y) / (float) magnitude) / ((players[i].x - hack_me.x) / (float) magnitude);
		deg = fabs((atan(arctan) * 360) / (2 * M_PI));

		//keep track of who has the smallest angle from my view to them
		if (fabs(mydeg - deg) < smallest && players[i].player_id != hack_me.player_id) {
			smallest = fabs(mydeg - deg);
			aimat = players[i].player_id;
			amag = magnitude;
		}

		//keep track of who's the nearest to me
		if ((magnitude < mindist || mindist == -1) && players[i].player_id != hack_me.player_id) {
			mindist = magnitude;
			aimat2 = players[i].player_id;
		}
	}
	//return who I'm closest to aiming at.
	//I'm thinking of modifying this... maps with lots of bots cause aim to be funny
	//because of lots of enemies at nearly the same angle but at varying distances...
	//the below algorithms don't work so well.  I'm thinking of defining varying
	//classes of distance and angle values... the more optimum values get a higher
	//class.  to determine who to aim at, a (possibly weighted) combination of the
	//angle class and distance class will determine who to aim at(higher value is
	//better)
	return aimat;
	
	if(smallest > -20 && smallest < 20) {
//		fprintf(output, "deg1: %f\n", smallest);
		return aimat;
	}
	else if(mindist < 1500) {
//		fprintf(output, "dist1: %f\n", mindist);
		return aimat2;
	}
	else if(amag < 160000 && (smallest > -40 && smallest < 40)) {
//		fprintf(output, "deg2: %f\n", smallest);
		return aimat;
	}
	else if (mindist < 6000) {
//		fprintf(output, "dist2: %f\n", mindist);
		return aimat2;
	} else {
//		fprintf(output, "dist3: %f\n", mindist);
		return aimat2;
	}
	fprintf(output, "No Target\n");
	return -1;
}

int findtarget_gren(player hack_me)
{
	int i, j;
	double magnitude, mindist = -1;
	int myteam, aimat2 = -1;
		
	//find my team for comparison with others(prevent tking)
	myteam = findteam(hack_me.client_id);
	if(me.client_id == hack_me.client_id || hack_me.client_id < 0)
		myteam = me_client.team_id;
	for (i = 0; i < numplayers; i++) { 
		if(numplayers == 2 && players[i].player_id != hack_me.player_id)
			return players[i].player_id;
		if(!players[i].visible)		//don't hit non-visible bots
			continue;
		if(!players[i].is_bot) {	//only check team if not a bot
			j = findteam(players[i].client_id);
			if(j == -1)
				continue;
			if(myteam == j) {
				//fprintf(output, "Friendly - Client: %d's team: %d\n", players[i].mover_id, j);
				continue;
			}
		}
		//distance is just about all I need to check.  Angle doesn't matter,
		//just hit the closest enemy
		magnitude = sqrt((pow((players[i].x - hack_me.x), 2) + pow((players[i].y - hack_me.y), 2)));
		
		if ((magnitude < mindist || mindist == -1) && players[i].player_id != hack_me.player_id &&
				magnitude > 1000) {
			mindist = magnitude;
			aimat2 = players[i].player_id;
		}
	}
	//fprintf(output, "This player: %d dist: %f\n", aimat2, mindist); 
	return aimat2; 
}	 

void docommand(char mesg[], char **tocl_end, char **tosv_end)
{
	char *mesgptr = &mesg[26], *id, *chat;
	char message[30], axis;
	short a1, a2;
	int size, i, dist, plr;
	short mesglen;

	//the length of our message
	memcpy(&mesglen, &mesg[1], sizeof(short));
	mesglen += 3;
	//lots of substring searches... find out which command we're running
	if(strstr(mesgptr, "autoaim") != 0)
	{
		autoaim = !autoaim;
		(autoaim)?GR_printf(tocl_end, 0, M_SV_CHAT, "enabling autoaim"):GR_printf(tocl_end, 0, M_SV_CHAT, "disabling autoaim");
	}
	else if(strstr(mesgptr, "autonade") != 0)
	{
		//take out nades for competition version
		autonade = !autonade;
		(autonade)?GR_printf(tocl_end, 0, M_SV_CHAT, "enabling autonade"):GR_printf(tocl_end, 0, M_SV_CHAT, "disabling autonade");
	}
	else if(strstr(mesgptr, "spoof") != 0)
	{
		id = strchr(mesgptr, ' ');
		id++;
		i = atoi(id);
		chat = strchr(id, ' ');
		*chat = 0;
		chat++;
		id = strchr(chat, (char)4);
		*id = 0;
		GR_printf(tosv_end, i, M_PL_CHAT, chat);
	}
	else if(strstr(mesgptr, "kick") != 0)
	{
		id = strchr(mesgptr, ' ');
		id++;
		i = atoi(id);
		GR_kick(i, tosv_end);
	}
	else if(strstr(mesgptr, "showall") != 0)
	{
		showall = !showall;
		GR_showall(showall, tocl_end);
		(showall)?GR_printf(tocl_end, 0, M_SV_CHAT, "enabling showing all players on the map"):GR_printf(tocl_end, 0, M_SV_CHAT, "disabling show all players");
	}
	else if(strstr(mesgptr, "fog") != 0)
	{
		fog = !fog;
		GR_fogoff(tocl_end);
		(fog)?GR_printf(tocl_end, 0, M_SV_CHAT, "removing fog"):GR_printf(tocl_end, 0, M_SV_CHAT, "resetting fog");
	}
	else if(strstr(mesgptr, "tele_dist") != 0)
	{
		//take out for competition version
		id = strchr(mesgptr, ' ');
		id++;
		axis = *id;

		id = strchr(id, ' ');
		id++;
		dist = atoi(id);

		GR_teleport_dist(tocl_end, tosv_end, axis,dist, &me);
		GR_printf(tocl_end, 0, M_SV_CHAT, "teleporting...");
	}
	else if(strstr(mesgptr, "tele") != 0)
	{
		id = strchr(mesgptr, ' ');
		id++;
		mesgptr = id;
		id = strchr(mesgptr, ' ');
		*id = 0;
		id++;
		plr = atoi(mesgptr);
		axis = *id;
		
		for(i = 0;i < numplayers; i++) {
			if(players[i].client_id == plr)
				break;
		}
		if(i >= numplayers) {
			return;
		}

		id = strchr(id, ' ');
		id++;
		dist = atoi(id);

		GR_teleport_dist(tocl_end, tosv_end, axis,dist, &players[i]);
		GR_printf(tocl_end, 0, M_SV_CHAT, "teleporting...");
	}
	/*else if(strstr(mesgptr, "hack_off") != 0)
	{
		hack_id.client_id = -1;
		GR_printf(tocl_end, 0, M_SV_CHAT, "client hack off", atoi(id));
	}
	else if(strstr(mesgptr, "hack") != 0)
	{
		//check to see if this works better after modification
		//if it still doesn't work, remove entirely.
		id = strchr(mesgptr, ' ');
		id++;
		hack_id.client_id = atoi(id);
		for(i = 0;i < numplayers; i++) {
			if(players[i].client_id == hack_id.client_id)
				break;
		}
		if(players[i].client_id == hack_id.client_id) {
			hack_id.player_id = players[i].player_id;
			hack_id.x = players[i].x;
			hack_id.y = players[i].y;
			hack_id.z = players[i].z;
			hack_id.angle1 = players[i].angle1;
			hack_id.angle2 = players[i].angle2;
			hack_id.a1 = anglec2f(players[i].angle1, 0);
			hack_id.a2 = anglec2f(players[i].angle1, 1);
		}
		GR_printf(tocl_end, 0, M_SV_CHAT, "hacking client %d", atoi(id));
	}*/
	else if(strstr(mesgptr, "suicide_off") != 0)
	{
		self_destruct = 0;
	}
	else if(strstr(mesgptr, "suicide") != 0)
	{
		//might be usefull to modify self_destroy code to get the
		//other team to tk each other.
		id = strchr(mesgptr, ' ');
		if(!id) {
			self_destruct = 1;
			self_destroy(tosv_end, tocl_end, -1);
		} else {
			id++;
			i = atoi(id);
			self_destroy(tosv_end, tocl_end, i);
		}
	}
	else if(strstr(mesgptr, "whois") != 0)
	{
		id = strchr(mesgptr, ' ');
		id++;
		i = atoi(id);
		GR_printf(tocl_end, i, M_PL_CHAT, "%d", i);
	}
	else if(strstr(mesgptr, "tk") != 0)
	{
		id = strchr(mesgptr, ' ');
		id++;
		i = atoi(id);
		
		teamkill(tosv_end, i);
	}
}		 

void self_destroy(char **tosv_end, char **tocl_end, int targ)
{
	char hitbullet[29] =
		"\x42\x1A\x00\xCA\x0B\x16\x00\x04\x00\x00\x00\x00\x94\x52\x15\x41\x8D\x7F\x28\x3F\x03\x00\x00\x00\x07\x8A\x84\x83\x00";
	char target;
	int i=0;
	float angle1, angle2;

	if(!self_destruct && targ < 0) //not mass suicide and no target to suicide
		return;

	if(targ > -1) {								//got a target to suicide... find the player
		for(i=0; i<numplayers;i++) {
			if(players[i].client_id == targ)
				break;
		}
	}
	if(targ < 0){									//no target, mass suicide.  skip non-visibles
		while(!players[i].visible && i < numplayers)
			continue;
	}
	if(i >= numplayers)						//make sure we're still within the array bounds
		return;
	
	//a little taunt to make it known there's a cheat
	GR_printf(tosv_end, players[i].client_id, M_PL_CHAT, "I will suicide to save my honor!");
	target = players[i].player_id;	//set target and build bullet
	hitbullet[7] = target;
	hitbullet[20] = (char) target;	//target is same as shooter ;)
	angle1 = 0;											//angle doesn't matter
	angle2 = 10;
	memcpy(&hitbullet[12], &angle1, sizeof(float));
	memcpy(&hitbullet[16], &angle2, sizeof(float));

	//send hacked suicide shot
	memcpy(*tosv_end, hitbullet, 29);
	*tosv_end += 29;
}

void GR_showall(int showall, char **tocl_end)
{
//	char mesg1[20] = "\x42\x10\x00\xBA\x0B\x0C\x00\x49\x01\x03\x00\x01\x01\x0A\x00\x14\x00\x00\x00";
//	char mesg2[20] = "\x42\x10\x00\xBA\x0B\x0C\x00\x48\x01\x03\x00\x01\x01\x0A\x00\x14\x00\x00\x00";
//	42 10 00 BA 0B 0C 00 0E 02 1F 00 51 01 0A 00 00 00 00 00 
//	42 10 00 BA 0B 0C 00 0F 02 2E 00 09 01 0A 00 00 00 00 00 
	int i;
	char showmap[20] =
		"\x42\x10\x00\xBA\x0B\x0C\x00\x0E\x02\x2E\x00\x09\x01\x0A\x00\x00\x00\x00\x00";
//		"\x42\x10\x00\xBA\x0B\x0C\x00\xF8\x01\x21\x00\x0C\x01\x0A\x00\x01\x00\x00\x00";

// add modifications to the mesg here
	if(showall)		//if showall cheat is toggled on
	{							//for every player, send show on map command
		for (i = 0; i < numplayers; i++) {
			memcpy(&showmap[9], &s_time, sizeof(short));
			showmap[15] = players[i].player_id;
//			showmap[13] = me.player_id;
//			showmap[15] = 0;
			memcpy(*tocl_end, showmap, 19);
			*tocl_end+=19;
		}
	} else {		//showall was turned off, send hide on map for
							//everone
		for (i = 0; i < numplayers; i++) {
			showmap[7] = (char)0x0F;
			showmap[8] = (char)0x02;
			showmap[15] = players[i].player_id;
			memcpy(&showmap[9], &s_time, sizeof(short));
			memcpy(*tocl_end, showmap, 19);
			*tocl_end+=19;
		}
	}
}

void GR_gamestart(char **tosv_end)
{
  char showmap1[17] =
		"\x42\x0D\x00\xBA\x0B\x09\x00\x40\x01\x4C\x00\xDB\x01\x01\x00\x00";
	char showmap2[14] = "\x42\x0A\x00\xBA\x0B\x06\x00\x2A\x01\x4C\x00\xDB\x00";
	char showmap3[26] =
		"\x42\x16\x00\xBA\x0B\x12\x00\x60\x00\x4C\x00\xDB\x02\x08\x00\x01\x00\x00\x00\x0B\x00\x00\x1A\x02\x04";
	char showmap4[50] = "\x42\x2E\x00\xBA\x0B\x2A\x00\x77\x01\x2C\x00\xEA\x05\x07\x00\xAB\x00\x0A\x00\x77\x00\x00\x00\xAB\x00\x00\x00\x00\x00\x00\x00\x00\x00\xAB\x00\x00\x00\x00\x00\x00\x00\x00\x00\x37\x00\x00\x00\x00\x00";
	char xup[20] =
		"\x42\x10\x00\xBA\x0B\x0C\x00\x26\x01\x61\x00\x38\x02\x04\x00\x01\x01\x00\x01";
	int i;
	// add modifications to the mesg here 
//	memcpy(&showmap1[9], &s_time, sizeof(short));
//	memcpy(&showmap2[9], &s_time, sizeof(short));
//	memcpy(&showmap3[9], &s_time, sizeof(short));
//	memcpy(&showmap4[9], &s_time, sizeof(short));
	
/*	memcpy(*tosv_end, showmap1, 16);
	*tosv_end+=16;
	memcpy(*tosv_end, showmap2, 13);
	*tosv_end+=13;
	memcpy(*tosv_end, showmap3, 25);
	*tosv_end+=25;*/

	for(i=0;i<numclients;i++) {
		memcpy(&xup[9], &s_time, sizeof(short));
		xup[15] = clients[i].client_id;
		memcpy(*tosv_end, xup, 19);
		*tosv_end+=19;
	}
}

void GR_kick(char id, char **tosv_end)
{
	int i;
	char showmap[31] = 
					"\x42\x1B\x00\xBA\x0B\x17\x00\x2B\x01\xB5\x00\x0A\x01\x11\x00\x1C\x00\x00\x00\x49\x43\x2A\x54\x72\x69\x67\x4D\x61\x6E\x00";
// add modifications to the mesg here
	showmap[12] = i;
	memcpy(&showmap[9], &s_time, sizeof(short));
	memcpy(*tosv_end, showmap, 30);
	*tosv_end+=30;
}

void GR_fogoff(char **tocl_end)
{
	char fogoff[47] =
		"\x42\x2B\x00\xBA\x0B\x27\x00\x83\x01\x3B\x00\x73\x06\x01\x00\x00\x08\x00\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x0C\x00\x00\x00\x00\x00\x0C\x00\x00\x00\x00\x00";
//		"\x42\x2B\x00\xBA\x0B\x27\x00\x76\x01\x10\x00\xE2\x06\x01\x00\x00\x08\x00\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x0C\x00\x00\x00\x00\x00\x0C\x00\x00\x00\x00\x00";

	if(fog) {
		//a simple static message will turn fog off for us...
		memcpy(&fogoff[9], &s_time, sizeof(short));
		memcpy(*tocl_end, fogoff, 46);
	} else {
		//a simple static message will turn fog back on for us...
		memcpy(&origfog[9], &s_time, sizeof(short));
		memcpy(*tocl_end, origfog, 46);
	}
	*tocl_end+=46;
}

//supposed to teleport me to the nearest enemy, but it doesn't work too well.
//this isn't being used and should be removed
void GR_teleport_plr(char **tocl_end, char **tosv_end)
{
	//short newx; 
	player key, *find;
	char aplayer;
	char tele[35] =
		"\x42\x1F\x00\xC4\x0B\x1B\x00\x1A\x00\xCA\x01\x01\x6D\x28\x1C\x09\xA9\x08\x00\x00\x00\x00\x00\x00\x19\x7F\x00\x00\x00\x00\x7F\x00\x00\xC0";

	aplayer = findtarget_gren(me);

	tele[11] = me.player_id;
	key.player_id = (char) aplayer;
	find = (player *)bsearch(&key, players, numplayers, sizeof(player), cmpmove);
	if(!find)
		return;
	me.x = find->x;
	me.y = find->y;
	me.z = find->z;
	memcpy(&tele[12], &me.x, sizeof(short));
	memcpy(&tele[14], &me.y, sizeof(short));
	memcpy(&tele[16], &me.z, sizeof(short));
	memcpy(&tele[7], &s_time, sizeof(short));
	tele[24] = (short) find->angle1;
	tele[25] = (short) find->angle2;

	memcpy(*tosv_end, tele, 34);
	*tosv_end+=34;
	memcpy(*tocl_end, tele, 34);
	*tocl_end+=34;
}

void GR_teleport_dist(char **tocl_end, char **tosv_end, char axis, int dist, player *hack_me)
{
	//short newx; 
	short mytime;
	int moved = 0, movedist = 247;
	char tele[35] =
		"\x42\x1F\x00\xC4\x0B\x1B\x00\x1A\x00\xCA\x01\x01\x6D\x28\x1C\x09\xA9\x08\x00\x00\x00\x00\x00\x00\x19\x7F\x00\x00\x00\x00\x7F\x00\x00\xC0";

	if(dist < 0) {		//if we're moving backwards, movement increments
		movedist *= -1;	//should be negative
	}

	//rewind time a little to simulate lag for our movement
	mytime = s_time-(moved/movedist);

	//send movement in increments of movedist
	while(abs(moved) < abs(dist))
	{
		if(abs(movedist+moved) > abs(dist))
			movedist = dist-moved;
		if(axis == 'x')
			hack_me->x += movedist;
		else if(axis == 'y')
			hack_me->y += movedist;
		else
			hack_me->z += movedist;
		//build movement message
		tele[11] = hack_me->player_id;
		memcpy(&tele[12], &hack_me->x, sizeof(short));
		memcpy(&tele[14], &hack_me->y, sizeof(short));
		memcpy(&tele[16], &hack_me->z, sizeof(short));
		memcpy(&tele[7], &mytime, sizeof(short));
		tele[24] = (short) hack_me->angle1;
		tele[25] = (short) hack_me->angle2;

		//send to both client and server to trick them both
		memcpy(*tosv_end, tele, 34);
		*tosv_end+=34;
		memcpy(*tocl_end, tele, 34);
		*tocl_end+=34;
		moved += movedist;
		mytime++;
	}
}

int GR_printf(char **to, char id, char type,char *format, ...)
{		 
	va_list vap;
	int size;
	short mesglen;
	int n; 
	char message[256];
	char mesg[256] = "\x42\x2F\x00\xBA\x0B\x2B\x00\x5A\x00\x2C\x08\xB5\x04\x04\x00\x00\x04\x00\x00\x37\x00";

	/*
		42 2A 00 BA 0B 1D 00 5B 00 10 00 66 04 04 00 00
		04 00 00 37 00 11 00 00 00 64 69 73 61 62 6C 69
		6E 67 20 61 75 74 6F 61 69 6D 04 00 00
		
		42 20 00 BA 0B 1C 00 5B 00 3E 00 5D 04 04 00 00
		04 00 00 37 00 07 00 00 00 74 65 73 74 69 6E 67
		04 00 01 
	*/
	va_start(vap, format);
	n = vsprintf(message, format, vap);																																				 va_end(vap);
	
	mesg[7] = type;
	mesg[15] = id;
	size = strlen(message);
	memcpy(&mesg[21], &size, sizeof(int));																																			message[size] = 4;
	size++;
	message[size] = 0;
	size++;
	(type == (char)M_SV_CHAT)?(message[size] = 0):(message[size] = 1);
	size++;
	memcpy(&mesg[25], &message, size);
	mesglen = size+21;
	memcpy(&mesg[5], &mesglen, sizeof(short));
	mesglen+=4;
	memcpy(&mesg[1], &mesglen, sizeof(short));
	mesglen+=3;
	memcpy(*to, mesg, mesglen);
	*to += mesglen;
//	if (send(socket, mesg, mesglen, 0) == -1) {
//		perror("Could not send chat message packet\n");
//		return -1;
//	}
	return n;
}

void addplayer(player temp)
{
	if(numplayers == MAXPLAYERS)
		return;
	memcpy(&players[numplayers], &temp, sizeof(player));
	if (numplayers > 1)
		qsort(players, (numplayers+1), sizeof(player), cmpmove);
	numplayers += 1;
	return;
}

void updateplayer(player temp)
{
	player * find;

	find = bsearch(&temp, players, numplayers, sizeof(player), cmpmove);
	if (find != NULL) {
		find->player_id = temp.player_id;
		find->x = temp.x;
		find->y = temp.y;
		find->z = temp.z;
		find->angle1 = temp.angle1;
		find->angle2 = temp.angle2;
		find->a1 = temp.a1;
		find->a2 = temp.a2;
	}
	return;
}

void visplayer(player temp)
{
	player * find;

	find = bsearch(&temp, players, numplayers, sizeof(player), cmpmove);
	if (find != NULL)
		find->visible = temp.visible;
	fprintf(output, "player changed visibility\n");
}

void remplayer(player temp)
{
	int i = 0;

	//fprintf(output, "Removing player %d: \n", temp.mover_id);
	while (players[i].player_id != temp.player_id && i < numplayers)
		i++;
	if (i >= numplayers)
		return;
	//fprintf(output, "Found player that died... Removing\n");
	//fprintf(output, "players[i].mover_id: %d\n", players[i].mover_id);
	for (; i < (numplayers - 1); i++) {
		memcpy(&players[i], &players[i + 1], sizeof(player));
	}
	memset(&players[(numplayers - 1)], 0, sizeof(player));
	qsort(players, (numplayers - 1), sizeof(player), cmpmove);
	numplayers -= 1;
}

void remclient(gr_client temp)
{
	int i = 0;

	while (clients[i].client_id != temp.client_id && i < numclients)
		i++;
	if (i >= numclients)
		return;
	//fprintf(output, "Client: %d exited\n", temp.client_id);
	for (; i < (numclients - 1); i++) {
		memcpy(&clients[i], &clients[i + 1], sizeof(gr_client));
	}
	memset(&clients[(numclients - 1)], 0, sizeof(gr_client));
	numclients -= 1;
}

void addclient(gr_client temp)
{
	memcpy(&clients[numclients], &temp, sizeof(gr_client));
	numclients += 1;
}

void updateclient(gr_client temp)
{
	int i=0;

	while (clients[i].client_id != temp.client_id && i < numclients)
		i++;
	if(i < numclients) {
		clients[i].team_id = temp.team_id;
	} else if (i >= numclients) {
		fprintf(output, "client not in list for update!\n");
		addclient(temp);
	}
}

//send out move packet to adjust angle.
void changeangle(char **tocl_end, char **tosv_end, player *pl, short angle1, short angle2){
	short mytime;
	char tele[35] =
								"\x42\x1F\x00\xC4\x0B\x1B\x00\x1A\x00\xCA\x01\x01\x6D\x28\x1C\x09\xA9\x08\x00\x00\x00\x00\x00\x00\x19\x7F\x00\x00\x00\x00\x7F\x00\x00\xC0";

	tele[11] = pl->player_id;
	mytime = s_time - 4;
	memcpy(&tele[12], &pl->x, sizeof(short));
	memcpy(&tele[14], &pl->y, sizeof(short));
	memcpy(&tele[16], &pl->z, sizeof(short));
//	s_time--;
	memcpy(&tele[7], &mytime, sizeof(short));
//	s_time++;
	tele[24] = (char) angle1;
	tele[25] = (char) angle2;

	//doing this twice with different times to hopefully make it appear that
	//we had aimed better/more accurately... make it show up better on replays
	memcpy(*tosv_end, tele, 34);
	*tosv_end+=34;
	mytime += 2;
	memcpy(&tele[7], &mytime, sizeof(short));
	memcpy(*tosv_end, tele, 34);
	*tosv_end+=34;
//don't have to send the new angles to the client
//wouldn't do any good since the server doesn't send
//our angles back to us... only to other players.
//	memcpy(*tocl_end, tele, 34);
//	*tocl_end+=34;
}

void teamkill(char **tosv_end, int tker){
	char hitbullet[29] =
		"\x42\x1A\x00\xCA\x0B\x16\x00\x04\x00\x00\x00\x00\x94\x52\x15\x41\x8D\x7F\x28\x3F\x03\x00\x00\x00\x07\x8A\x84\x83\x00";
	//CA = hit bullet. 
	int target;
	player key, *find;
	int magnitude, i;
	float angle1, angle2;

	for(i=0; i<numplayers;i++) {
		if(players[i].client_id == tker)
			break;
	}

	if(i >= numplayers)
		return;

	for(target=0; target<numplayers; target++) {
		//prepare information for a new hacked hit
		hitbullet[7] = players[i].player_id;
		//update my angle based off of the missed shot
		//find a target to shoot at
		if (findteam(players[target].player_id) != findteam(players[i].player_id))
			continue;
		//set up to hit our target
		key.player_id = hitbullet[20] = (char) players[target].player_id;
		//get info of the target so we can aim at him
		find = bsearch(&key, players, numplayers, sizeof(player), cmpmove);
		if (find == NULL)	//didn't find our target... real miss
			continue;
		//distance to our target... trig ;)
		magnitude = 
			sqrt(pow((find->x - players[i].x), 2) + pow((find->y - players[i].y), 2));
		//horizontal angle to our target(sin for 1 cos for 2) + 10 on 2(GR magic)
		angle1 = (find->x - players[i].x) / (float) magnitude;
		angle2 = 10 + ((find->y - players[i].y) / (float) magnitude);
		memcpy(&hitbullet[12], &angle1, sizeof(float));
		memcpy(&hitbullet[16], &angle2, sizeof(float));
	
		//copy the hit into output buffer to send later
		memcpy(*tosv_end, hitbullet, 29);
		*tosv_end += 29;
	}
}

void fire(char **tosv_end){
	char hitbullet[29] =
		"\x42\x1A\x00\xCA\x0B\x16\x00\x04\x00\x00\x00\x00\x94\x52\x15\x41\x8D\x7F\x28\x3F\x03\x00\x00\x00\x07\x8A\x84\x83\x00";
	//CA = hit bullet. 
	int target;
	player key, *find;
	int magnitude, i;
	float angle1, angle2;

	//prepare information for a new hacked hit
	hitbullet[7] = hack_id.player_id;
	//update my angle based off of the missed shot
	//find a target to shoot at
	target = findtarget(hack_id);
	if (target < 0)
		return;
	//set up to hit our target
	key.player_id = hitbullet[20] = (char) target;
	//get info of the target so we can aim at him
	find = bsearch(&key, players, numplayers, sizeof(player), cmpmove);
	if (find == NULL)	//didn't find our target... real miss
		return;
	//distance to our target... trig ;)
	magnitude = 
		sqrt(pow((find->x - hack_id.x), 2) + pow((find->y - hack_id.y), 2));
	//horizontal angle to our target(sin for 1 cos for 2) + 10 on 2(GR magic)
	angle1 = (find->x - hack_id.x) / (float) magnitude;
	angle2 = 10 + ((find->y - hack_id.y) / (float) magnitude);
	memcpy(&hitbullet[12], &angle1, sizeof(float));
	memcpy(&hitbullet[16], &angle2, sizeof(float));

	//copy the hit into output buffer to send later
	memcpy(*tosv_end, hitbullet, 29);
	*tosv_end += 29;
}

void serveredit(char client_id, char **tocl_end, char **tosv_end) {
	char edit_server1[17] = "\x42\x0D\x00\xBA\x0B\x09\x00\x32\x01\xA0\x02\xA9\x01\x04\x00\x01";
	char edit_server2[17] = "\x42\x0D\x00\xBA\x0B\x09\x00\x2D\x01\xA0\x02\xA9\x01\x04\x00\x01";

	edit_server1[15] = client_id;
	edit_server2[15] = client_id;
	memcpy(&edit_server1[9], &s_time, sizeof(short));
	memcpy(&edit_server2[9], &s_time, sizeof(short));

	//copy the data into output buffer to send later
	memcpy(*tocl_end, edit_server1, 16);
	*tocl_end += 16;
	memcpy(*tocl_end, edit_server2, 16);
	*tocl_end += 16;

	//copy the data into output buffer to send later
	edit_server1[15] = 0;
	edit_server2[15] = 0;
	
	memcpy(*tosv_end, edit_server1, 16);
	*tosv_end += 16;
	memcpy(*tosv_end, edit_server2, 16);
	*tosv_end += 16;
}

void editfin(char **tocl_end, char **tosv_end) {
	char end_edit[20] = "\x42\x10\x00\xBA\x0B\x0C\x00\x2F\x01\xB0\x01\xD4\x02\x04\x00\x00\x01\x00\x00";

	memcpy(&end_edit[9], &s_time, sizeof(short));

	//copy the data into output buffer to send later
	//memcpy(*tocl_end, edit_server1, 16);
//	*tocl_end += 16;
//	memcpy(*tocl_end, edit_server2, 16);
//	*tocl_end += 16;

	//copy the data into output buffer to send later
	memcpy(*tosv_end, end_edit, 19);
	*tosv_end += 19;
	memcpy(*tosv_end, end_edit, 19);
	*tosv_end += 19;
}
