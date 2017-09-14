#include <sys/socket.h>
#include <sys/un.h>
#include <utils/Errors.h>
#include <utils/Log.h>

#define UNIX_DOMAIN "/data/user/0/nativervc.domain"
//#define LOG_NDEBUG 0
//#define LOG_TAG "RvcTest"

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
    int socketfd = -1;
    while(true) {
        socketfd = connect_socket();
        if (socketfd < 0) {
			continue;
		} else {
			ALOGD("Successfully connected to Rvc!");
		}
		bool flag = false;
        while(!flag) {
            char msg[4];
			msg[0] = 60;
			msg[1] = 20;
			msg[2] = 30;
			msg[3] = 40;
            send_msg(socketfd, msg, 4);
			//flag = true;
        }
        //close(socketfd);
    }
}
