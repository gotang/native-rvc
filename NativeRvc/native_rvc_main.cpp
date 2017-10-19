#define LOG_TAG "NativeRvc"
#include <cutils/properties.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <utils/Log.h>
#include <utils/threads.h>

#if defined(HAVE_PTHREADS)
# include <pthread.h>
# include <sys/resource.h>
#endif

#include "RvcTrack.hpp"
#include "RvcCamera.hpp"
#include "RvcSocket.hpp"
#include "RvcTrackReceiver.hpp"

using namespace android;

#define OPT_LIST "a:b:c:d:w:h:L:W:D:H:M:N:sS"

struct opt_args {
	bool showUsage;
    int angle;
	int oneMPercent;
    int twoMPercent;
    int threeMPercent;
    bool useSocket;
    int wheelBase;
    int axialLength;
    int rearAxle;
    int camHeight;
    int camVisualAngle;
    int camHorAngle;
    int cameraWidth;
    int cameraHeight;
};

void show_usage(void)
{
	printf("Usage: native rvc [options]\n");
	printf("    -S                  show this help.\n");
	printf("    -a <angle>          left track deflection angle, < 90, default 30.\n");
	printf("    -b <oneMPercent>    one meter line's percent from the bottom of the screen, < 100, default 10.\n");
	printf("    -c <twoMPercent>    two meter line's percent from the bottom of the screen, < 100, default 20.\n");
	printf("    -d <threeMPercent>  three meter line's percent from the bottom of the screen, < 100, default 30.\n");
    printf("    -w <width>          camera width.\n");
    printf("    -h <height>         camera height.\n");
    printf("    -L <wheelbase>      wheelbase(mm)\n");
    printf("    -W <axiallength>    axial length(mm)\n");
    printf("    -D <length>         The length of the rear axle from the end of the vehicle(mm).\n");
    printf("    -H <height>         camera distance from ground(mm).\n");
    printf("    -M <angle>          visual angle range of camera.\n");
    printf("    -N <angle>          The angle between the camera center line and the horizontal plane.\n");
	printf("    -s                  use socket to communicate with nativervc.\n");
	printf("\n");
}

int parse_args(int argc, char *argv[], struct opt_args* args)
{
	int ch = 0;
	int err = 0;

	memset(args, 0, sizeof(*args));

	opterr=0;
	while ((ch=getopt(argc, argv, OPT_LIST)) != -1) {
		char * endch;
		switch (ch) {
    		case 'S':
    			args->showUsage = true;
    			show_usage();
    			break;
            case 'a':
                args->angle = atoi(optarg);
    			if (args->angle > 90 || args->angle < 0) err=1;
                break;
            case 'b':
    			args->oneMPercent = atoi(optarg);
    			if (args->oneMPercent < 0) err=1;
                break;
            case 'c':
                args->twoMPercent = atoi(optarg);
    			if (args->twoMPercent < 0) err=1;
                break;            
    		case 'd':
    			args->threeMPercent = atoi(optarg);
    			if (args->threeMPercent < 0) err=1;
    			break;
            case 'w':
                args->cameraWidth = atoi(optarg);
                if (args->cameraWidth < 0) err=1;
                break;
            case 'h':
                args->cameraHeight = atoi(optarg);
                if (args->cameraHeight < 0) err=1;
                break;
    		case 's':
    			args->useSocket = true;
    		    break;
            case 'L':
                args->wheelBase = atoi(optarg);
                if (args->wheelBase < 0) err=1;
                break;
            case 'W':
                args->axialLength = atoi(optarg);
                if (args->axialLength < 0) err=1;
                break;
            case 'D':
                args->rearAxle = atoi(optarg);
                if (args->rearAxle < 0) err=1;
                break;
            case 'H':
                args->camHeight = atoi(optarg);
                if (args->camHeight < 0) err=1;
                break;
            case 'M':
                args->camVisualAngle = atoi(optarg);
                if (args->camVisualAngle < 0) err=1;
                break;
            case 'N':
                args->camHorAngle = atoi(optarg);
                if (args->camHorAngle < 0) err=1;
                break;
    		default:
    			ch = optopt;
    			err = 1;
    			break;
		}
		if (err) {
			printf("Invalid option: -%c %s", ch, optarg);
			return -1;
		}
	}
	return 0;
}
// ---------------------------------------------------------------------------
//For exsample
//nativervc -a 60 -b 50 -c 60 -d 70 -w 1920 -h 1080 -L 2660 -W 1650 -D 500 -H 600 -M 120 -N 30
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    ALOGD("NativeRvc main");
#if defined(HAVE_PTHREADS)
    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_DISPLAY);
#endif
	struct opt_args args;
	if(parse_args(argc, argv, &args) < 0) {
		return -1;
	}
	if (args.showUsage) return 0;
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();
    //sp<RvcCamera> cam = new RvcCamera(args.cameraWidth, args.cameraHeight);
    //while(!cam->getPreviewState()) usleep(500000);
    TrackParams_t params;
	memset(&params, 0, sizeof(params));
    params.angle = args.angle;
	if (params.angle == 0) params.angle = 30;
    params.oneMeterPercent = args.oneMPercent;
	if (params.oneMeterPercent == 0) params.oneMeterPercent = 10;
    params.twoMeterPercent = args.twoMPercent;
	if (params.twoMeterPercent == 0) params.twoMeterPercent = 20;
    params.threeMeterPercent = args.threeMPercent;
	if (params.threeMeterPercent == 0) params.threeMeterPercent = 30;
    params.wheelBase = args.wheelBase;
    params.axialLength= args.axialLength;
    params.rearAxle = args.rearAxle;
    params.camHeight = args.camHeight;
    params.camVisualAngle = args.camVisualAngle;
    params.camHorAngle = args.camHorAngle;
    sp<RvcTrack> track = new RvcTrack(params,args.cameraWidth, args.cameraHeight);
    if (args.useSocket) {
        sp<RvcSocket> socket = new RvcSocket(track);
    }
    sp<RvcTrackReceiver> receiver = new RvcTrackReceiver(track);
    IPCThreadState::self()->joinThreadPool();

    return 0;
}

