#define LOG_NDEBUG 0
#define LOG_TAG "RvcCamera"

#include <stdint.h>
#include <sys/types.h>
#include <math.h>
#include <fcntl.h>
#include <utils/misc.h>
#include <signal.h>

#include <cutils/properties.h>

#include <utils/Atomic.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/threads.h>

#include <GLES/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <EGL/eglext.h>
#include <binder/IPCThreadState.h>

#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <ui/DisplayInfo.h>

#include "RvcCamera.hpp"
#include <camera/CameraBase.h>

#define PREVIEW_FRAME_RATE 30
#define RVC_EXIT_PROP "usr.rvc.exit"

namespace android {
// ---------------------------------------------------------------------------

RvcCamera::RvcCamera(): Thread(false) {
    ALOGD("RvcCamera RvcCamera");
    mSession = new SurfaceComposerClient();
    mPreviewStarted = false;
}

RvcCamera::~RvcCamera() {
    if (mCameraParams) {delete mCameraParams; mCameraParams=NULL;}
}

bool RvcCamera::threadLoop() {
    ALOGD("RvcCamera threadLoop");
    startPreview();
    while (!exitPending()) {
        sleep(1);
        checkExit();
    }
    stopPreview();
    property_set(RVC_EXIT_PROP, "0");
    mFlingerSurface.clear();
    mFlingerSurfaceControl.clear();
    IPCThreadState::self()->stopProcess();
    return false;
}

void RvcCamera::checkExit() {
    char value[PROPERTY_VALUE_MAX];
    property_get(RVC_EXIT_PROP, value, "0");
    int exitnow = atoi(value);
    if (exitnow) {
        requestExit();
    }
}

sp<SurfaceComposerClient> RvcCamera::session() const {    
    return mSession;
}

status_t RvcCamera::readyToRun() {
    ALOGD("RvcCamera readyToRun");
    sp<IBinder> dtoken(SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain));
    DisplayInfo dinfo;
    status_t status = SurfaceComposerClient::getDisplayInfo(dtoken, &dinfo);
    if (status)
        return -1;
	if (dinfo.w < dinfo.h) {
		int tmp = 0;
		tmp = dinfo.w;
		dinfo.w = dinfo .h;
		dinfo.h = tmp;
	}
    // create the native surface
    sp<SurfaceControl> control = session()->createSurface(String8("RvcCamera"),
            dinfo.w, dinfo.h, PIXEL_FORMAT_RGB_565);

    SurfaceComposerClient::openGlobalTransaction();
    control->setLayer(0x40000001);
    SurfaceComposerClient::closeGlobalTransaction();

    sp<Surface> s = control->getSurface();

    mFlingerSurfaceControl = control;
    mFlingerSurface = s;

    openCamera();
    initCameraParams();

    return NO_ERROR;
}

void RvcCamera::onFirstRef() {
    ALOGD("RvcCamera onFirstRef");
    status_t err = mSession->linkToComposerDeath(this);
    ALOGE_IF(err, "linkToComposerDeath failed (%s) ", strerror(-err));
    if (err == NO_ERROR) {
        run("RvcCamera", PRIORITY_DISPLAY);
    }
    property_set(RVC_EXIT_PROP, "0");
}

void RvcCamera::binderDied(const wp<IBinder>& who) {
    ALOGD("RvcCamera binderDied");
    // woah, surfaceflinger died!
    ALOGD("SurfaceFlinger died, exiting...");

    // calling requestExit() is not enough here because the Surface code
    // might be blocked on a condition variable that will never be updated.
    property_set(RVC_EXIT_PROP, "1");
    kill( getpid(), SIGKILL );
    requestExit();
}

void RvcCamera::openCamera() {
    ALOGD("RvcCamera openCamera");
    //camera
    int numberOfCameras = Camera::getNumberOfCameras();
    CameraInfo cameraInfo;
    for (int i = 0; i < numberOfCameras; i++) {
        Camera::getCameraInfo(i, &cameraInfo);
        if (cameraInfo.facing == 0) {
            String16 name("NativeRvc");
#ifdef FOR_ANDROID_NN
            mCamera = Camera::connect(i, name, Camera::USE_CALLING_UID, Camera::USE_CALLING_PID);
#else
            mCamera = Camera::connect(i, name, Camera::USE_CALLING_UID);
#endif
            break;
        }
    }

    if (mCamera == NULL) {
        ALOGE("Fail to connect to camera service");
        return;
    }
    // make sure camera hardware is alive
    if (mCamera->getStatus() != NO_ERROR) {
        ALOGE("Camera initialization failed");
        return;
    }
}

void RvcCamera::initCameraParams() {
    ALOGD("RvcCamera initCameraParams");
    String8 param = mCamera->getParameters();
    mCameraParams = new CameraParameters2();
    mCameraParams->unflatten(param);
    ALOGD("preview format %s ", mCameraParams->getPreviewFormat());
    mCameraParams->setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
    mCameraParams->setPreviewFrameRate(PREVIEW_FRAME_RATE);
    mCameraParams->setPreviewSize(1920, 1080);
    mCamera->setParameters(mCameraParams->flatten());
    mCamera->setPreviewCallbackFlags(CAMERA_FRAME_CALLBACK_FLAG_NOOP);
    sp<IGraphicBufferProducer> gbp = mFlingerSurface->getIGraphicBufferProducer();
    mCamera->setPreviewTarget(gbp);
}

void RvcCamera::startPreview() {
    ALOGD("RvcCamera startPreview");
    mCamera->startPreview();
    mPreviewStarted = true;
}

void RvcCamera::stopPreview() {
    ALOGD("RvcCamera stopPreview");
    mCamera->stopPreview();
    mPreviewStarted = false;
}

bool RvcCamera::getPreviewState() {
    return mPreviewStarted;
}

// ---------------------------------------------------------------------------
}; //namespace android

