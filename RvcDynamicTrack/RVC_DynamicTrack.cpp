#include <sys/socket.h>
#include <sys/un.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#define UNIX_DOMAIN "/data/user/0/nativervctrack.domain"
//#define LOG_NDEBUG 0
//#define LOG_TAG "RvcTest"

enum {
    UP_KEY = 0,
    DOWN_KEY,
    LEFT_KEY,
    RIGHT_KEY,
};

void show()
{
	printf("Input Track Params\n");
	printf("l [angle]          |left track deflection angle.\n");
	printf("r [angle]          |right track deflection angle.\n");
    printf("D                  |adjust wheel angle +1 or -1.\n");
    printf("q                  |quit.\n");
}

int connect_socket() {
    int fd = -1;
    int reuse = 1;
    struct sockaddr_un sa_remote;
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        ALOGD("socket failed!");
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&sa_remote, 0, sizeof(sa_remote));
    sa_remote.sun_family = AF_UNIX;
    strncpy(sa_remote.sun_path, UNIX_DOMAIN, strlen(UNIX_DOMAIN));
    if (connect(fd, (struct sockaddr*)&sa_remote, sizeof(sa_remote)) < 0) {
        ALOGD("connect failed! close fd=%d, errno=%d, %s", fd, errno, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int send_msg(int fd, void *msg, int size)
{
	int err = -1;
	err = send(fd, msg, size, MSG_NOSIGNAL);
	return err;
}

int main(int argc, char** argv)
{
    show();
    char input[10];
    int socketfd = -1;
    bool stop = false;
    while(!stop) {
        socketfd = connect_socket();
        if (socketfd < 0) {
			continue;
		} else {
			ALOGD("Successfully connected to Rvc!");
		}
        while(!stop) {
            printf("->");
            fgets(input, sizeof(input), stdin);
            if ((input[1] != ' ') && (input[1] != '\n')) {
                printf("Invalid params!\n");
                continue;
            }
            switch(input[0]) {
                case 'l':
                case 'r':
                    send_msg(socketfd, input, strlen(input)-1);
                    break;
                case 'D':
                    char inputchar[2];
                    inputchar[0] = input[0];
                    char ch1, ch2;
                    static struct termios oldt, newt;
                    tcgetattr(0, &oldt);
                    newt = oldt;
                    newt.c_lflag &= ~(ICANON | ECHO);
                    tcsetattr(0, TCSANOW, &newt);
                    while ((ch1 = getchar()) != '\n') {
                        ch2 = getchar();
                        if (ch1 == 27 && ch2 == 91) {
                            switch(getchar()){
                               case 65:
                                   inputchar[1] = UP_KEY;
                                   break;
                               case 66:
                                   inputchar[1] = DOWN_KEY;
                                   break;
                               case 68:
                                   inputchar[1] = LEFT_KEY;
                                   break;
                               case 67:
                                   inputchar[1] = RIGHT_KEY;
                                   break;
                               default:
                                   break;
                            }       
                            send_msg(socketfd, inputchar, sizeof(inputchar));
                        } else {
                            printf("Invalid params!\n");
                        }
                    }
                    tcsetattr(0, TCSANOW, &oldt);
                    break;
                case 'q':
                    stop = true;
                    break;
                default:
                    printf("Invalid params!\n");
                    break;
            };
        }
    }
}
