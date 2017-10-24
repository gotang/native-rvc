#define LOG_NDEBUG 0
#define LOG_TAG "RvcTrack"

#include <stdint.h>
#include <sys/types.h>
#include <math.h>
#include <fcntl.h>
#include <utils/misc.h>
#include <signal.h>
#include <cutils/properties.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <ui/DisplayInfo.h>
#include <utils/Atomic.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include <GLES/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <EGL/eglext.h>
#include <SkString.h>
#include <SkPaint.h>
#include <SkCanvas.h>
#include <SkStream.h>
#include <SkBitmap.h>
#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <math.h>
#include <string.h>

#include "RvcTrack.hpp"

#define DEFAULT_FONT_HEIGHT 60

#define RVC_EXIT_PROP "usr.rvc.exit"

namespace android {

struct Texture {
    GLint   w;
    GLint   h;
    GLuint  name;
};

enum {
    UP_KEY = 0,
    DOWN_KEY,
    LEFT_KEY,
    RIGHT_KEY,
};

// ---------------------------------------------------------------------------
RvcTrack::RvcTrack(TrackParams_t& params, int camW, int camH) : Thread(false) {
    mSession = new SurfaceComposerClient();
    mWidth = 0;
    mHeight = 0;
    mCamWidth = camW;
    mCamHeight = camH;
    mFps = 30;
    mTrackParams = params;
    mPaint = NULL;
    mCanvas = NULL;
    mBitmap = NULL;
    mTexture = 0;
    mMutex = new Mutex();
    memset(&mDynamicTrack, 0, sizeof(mDynamicTrack));
    ALOGD("wheelBase=%d,axialLength=%d,rearAxle=%d,camHeight=%d,camVisualAngle=%d,camVerAngle=%d",
        mTrackParams.wheelBase,mTrackParams.axialLength,mTrackParams.rearAxle,mTrackParams.camHeight,
        mTrackParams.camVisualAngle,mTrackParams.camVerAngle);
}

RvcTrack::~RvcTrack() {
    glDeleteTextures(1, &mTexture);
    mTexture = 0;
    if (mPaint)  { delete mPaint;  mPaint  = NULL; }
    if (mCanvas) { delete mCanvas; mCanvas = NULL; }
    if (mBitmap) { delete mBitmap; mBitmap = NULL; }
    if (mMutex)  { delete mMutex;  mMutex  = NULL; }
}

void RvcTrack::binderDied(const wp<IBinder>&) {
    // woah, surfaceflinger died!
    ALOGD("SurfaceFlinger died, exiting...");

    // calling requestExit() is not enough here because the Surface code
    // might be blocked on a condition variable that will never be updated.    
    property_set(RVC_EXIT_PROP, "1");
    kill(getpid(), SIGKILL);
    requestExit();
}

void RvcTrack::onFirstRef() {
    status_t err = mSession->linkToComposerDeath(this);
    ALOGE_IF(err, "linkToComposerDeath failed (%s) ", strerror(-err));
    if (err == NO_ERROR) {
        run("RvcTrack", PRIORITY_DISPLAY);
    }
    property_set(RVC_EXIT_PROP, "0");
}

status_t RvcTrack::readyToRun() {
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
    sp<SurfaceControl> control = mSession->createSurface(
            String8("RvcTrack"), dinfo.w, dinfo.h, PIXEL_FORMAT_RGB_565);

    SurfaceComposerClient::openGlobalTransaction();
    control->setLayer(0x40000002);
    SurfaceComposerClient::closeGlobalTransaction();
    sp<Surface> s = control->getSurface();

    // initialize opengl and egl
    const EGLint attribs[] = {
            EGL_RED_SIZE,   8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
    };
    EGLint w, h, dummy;
    EGLint numConfigs;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    eglInitialize(display, 0, 0);
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);
    surface = eglCreateWindowSurface(display, config, s.get(), NULL);
    context = eglCreateContext(display, config, NULL, NULL);
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
        return NO_INIT;

    mDisplay = display;
    mContext = context;
    mSurface = surface;
    mWidth = w;
    mHeight = h;
    mFlingerSurfaceControl = control;
    mFlingerSurface = s;

    ALOGD("%s %d: mWidth=%u, mHeight=%u\n", __FUNCTION__, __LINE__, mWidth, mHeight);

    return NO_ERROR;
}

void RvcTrack::init(int w, int h) {
    if (!h) {
        h = DEFAULT_FONT_HEIGHT;
    }
    if (!mPaint) {
        mPaint = new SkPaint();
        mPaint->setStyle(SkPaint::kStroke_Style);
        mPaint->setAntiAlias(true);
        mPaint->setStrokeWidth(5.0f);
        mPaint->setAlpha(0xFF);
    }
    if (!w) {
        w = SkScalarRoundToInt(mPaint->measureText("888888.8", 8));
    }
    if (!mBitmap) {
        mBitmap = new SkBitmap();
#ifdef FOR_ANDROID_KK
        mBitmap->setConfig(SkBitmap::kARGB_8888_Config, w, h);
        mBitmap->allocPixels();
#else
        SkImageInfo info = SkImageInfo::Make(w, h, kN32_SkColorType, kPremul_SkAlphaType);
        mBitmap->allocPixels(info);
#endif
    }
    if (!mCanvas) {
        mCanvas = new SkCanvas(*mBitmap);
    }
    if (!mTexture) {
        glGenTextures(1, &mTexture);
        glBindTexture(GL_TEXTURE_2D, mTexture);
        GLint crop[4] = { 0, h, w, -h };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
}

void RvcTrack::setStaicCoordinate() {
    mPoint[0].fx = getScreenXCoordinate(-mTrackParams.axialLength/2,0)*mWidth/mCamWidth+mWidth/2;
	mPoint[1].fx = mWidth - mPoint[0].fx;
    mPoint[1].fy = mPoint[0].fy = mHeight;
    mPoint[2].fy = mPoint[3].fy = mHeight*(100-mTrackParams.oneMeterPercent)/100;
    mPoint[4].fy = mPoint[5].fy = mHeight*(100-mTrackParams.twoMeterPercent)/100;
    mPoint[6].fy = mPoint[7].fy = mHeight*(100-mTrackParams.threeMeterPercent)/100;
    mPoint[2].fx = tan((90-mTrackParams.angle)*M_PI/180)*(mHeight-mPoint[2].fy)+mPoint[0].fx;
    mPoint[3].fx = mWidth-mPoint[2].fx;
    mPoint[4].fx = tan((90-mTrackParams.angle)*M_PI/180)*(mHeight-mPoint[4].fy)+mPoint[0].fx;
    mPoint[5].fx = mWidth-mPoint[4].fx;
    mPoint[6].fx = tan((90-mTrackParams.angle)*M_PI/180)*(mHeight-mPoint[6].fy)+mPoint[0].fx;
    mPoint[7].fx = mWidth-mPoint[6].fx;
    for(int i=0; i<8; i++) {
        ALOGD("screenPoint i=%d, x=%f, y=%f", i, mPoint[i].fx, mPoint[i].fy);
    }
}

double RvcTrack::getGroundXCoordinate(const DynamicTrack_t& params, double radius, double y) {
    double x = 0;
    double temp = 0;
    double circleX = mTrackParams.wheelBase / tan(params.angle*M_PI/180);
    temp = radius*radius - (y+mTrackParams.rearAxle)*(y+mTrackParams.rearAxle);
    if (params.direction == 'l') {
        x = sqrt(temp) - circleX;
    } else if (params.direction == 'r') {
        x = circleX - sqrt(temp);
    }
    return x;
}

double RvcTrack::getScreenXCoordinate(double x, double y) {
    double X = 0;
    X = x*((mCamHeight/(2*tan(mTrackParams.camVisualAngle/2*M_PI/180)))*cos(atan(y/mTrackParams.camHeight)))/(mTrackParams.camHeight*cos(atan(y/mTrackParams.camHeight)-mTrackParams.camVerAngle*M_PI/180));
    return X*(mWidth/mCamWidth);
}

double RvcTrack::getScreenYCoordinate(double y) {
    double Y = 0;
    Y = (mCamHeight/2)*(1/tan(mTrackParams.camVisualAngle/2*M_PI/180))*(tan(atan(y/mTrackParams.camHeight)-mTrackParams.camVerAngle*M_PI/180));
    return Y*(mHeight/mCamHeight);
}

void RvcTrack::setDynamicCoordinate(const DynamicTrack_t& params) {
    if (params.angle > 30) return;
    double trackRC, trackRI, trackRO;
    trackRC = mTrackParams.wheelBase/tan(params.angle*M_PI/180);
    trackRI = trackRC - mTrackParams.axialLength/2;
    trackRO = trackRC + mTrackParams.axialLength/2;
	ALOGD("trackRC=%f,trackRI=%f,trackRO=%f,angle=%f", trackRC,trackRI,trackRO);
    Point groundPoint[9];
    //0-2:center line
    //3-5:inside line
    //6-8:outside line
    groundPoint[0].fy = 500;
    groundPoint[0].fx = getGroundXCoordinate(params, trackRC, groundPoint[0].fy);
    groundPoint[1].fy = 1000;
    groundPoint[1].fx = getGroundXCoordinate(params, trackRC, groundPoint[1].fy);
    groundPoint[2].fy = 1500;
    groundPoint[2].fx = getGroundXCoordinate(params, trackRC, groundPoint[2].fy);
    groundPoint[3].fy = (groundPoint[0].fy+mTrackParams.rearAxle)*trackRI/trackRC-mTrackParams.rearAxle;
    groundPoint[3].fx = getGroundXCoordinate(params, trackRI, groundPoint[3].fy);
    groundPoint[4].fy = (groundPoint[1].fy+mTrackParams.rearAxle)*trackRI/trackRC-mTrackParams.rearAxle;
    groundPoint[4].fx = getGroundXCoordinate(params, trackRI, groundPoint[4].fy);
    groundPoint[5].fy = (groundPoint[2].fy+mTrackParams.rearAxle)*trackRI/trackRC-mTrackParams.rearAxle;
    groundPoint[5].fx = getGroundXCoordinate(params, trackRI, groundPoint[5].fy);
    groundPoint[6].fy = (groundPoint[0].fy+mTrackParams.rearAxle)*trackRO/trackRC-mTrackParams.rearAxle;
    groundPoint[6].fx = getGroundXCoordinate(params, trackRO, groundPoint[6].fy);
    groundPoint[7].fy = (groundPoint[1].fy+mTrackParams.rearAxle)*trackRO/trackRC-mTrackParams.rearAxle;
    groundPoint[7].fx = getGroundXCoordinate(params, trackRO, groundPoint[7].fy);
    groundPoint[8].fy = (groundPoint[2].fy+mTrackParams.rearAxle)*trackRO/trackRC-mTrackParams.rearAxle;
    groundPoint[8].fx = getGroundXCoordinate(params, trackRO, groundPoint[8].fy);
    for(int i=0; i<9; i++) {
        ALOGD("groundPoint i=%d, x=%f, y=%f", i, groundPoint[i].fx, groundPoint[i].fy);
    }
    //temp screen point
    Point tempPoint[6];
    //0-2:inside line
    //3-5:outside line
    tempPoint[0].fy = getScreenYCoordinate(groundPoint[3].fy);
    tempPoint[0].fx = getScreenXCoordinate(groundPoint[3].fx, groundPoint[3].fy);
    tempPoint[1].fy = getScreenYCoordinate(groundPoint[4].fy);
    tempPoint[1].fx = getScreenXCoordinate(groundPoint[4].fx, groundPoint[4].fy);
    tempPoint[2].fy = getScreenYCoordinate(groundPoint[5].fy);
    tempPoint[2].fx = getScreenXCoordinate(groundPoint[5].fx, groundPoint[5].fy);
    tempPoint[3].fy = getScreenYCoordinate(groundPoint[6].fy);
    tempPoint[3].fx = getScreenXCoordinate(groundPoint[6].fx, groundPoint[6].fy);
    tempPoint[4].fy = getScreenYCoordinate(groundPoint[7].fy);
    tempPoint[4].fx = getScreenXCoordinate(groundPoint[7].fx, groundPoint[7].fy);
    tempPoint[5].fy = getScreenYCoordinate(groundPoint[8].fy);
    tempPoint[5].fx = getScreenXCoordinate(groundPoint[8].fx, groundPoint[8].fy);
    for(int i=0; i<6; i++) {
        ALOGD("tempPoint i=%d, x=%f, y=%f", i, tempPoint[i].fx, tempPoint[i].fy);
    }
	mPoint[0].fx = getScreenXCoordinate(-mTrackParams.axialLength/2,0)*mWidth/mCamWidth+mWidth/2;
	mPoint[1].fx = mWidth - mPoint[0].fx;
    mPoint[1].fy = mPoint[0].fy = mHeight;
    if (params.direction == 'l') {
        ALOGD("direction is left");
        mPoint[2].fx = mWidth/2 + tempPoint[0].fx;
        mPoint[2].fy = mHeight - tempPoint[0].fy;
        mPoint[4].fx = mWidth/2 + tempPoint[1].fx;
        mPoint[4].fy = mHeight - tempPoint[1].fy;
        mPoint[6].fx = mWidth/2 + tempPoint[2].fx;
        mPoint[6].fy = mHeight - tempPoint[2].fy;
        mPoint[3].fx = mWidth/2 + tempPoint[3].fx;
        mPoint[3].fy = mHeight - tempPoint[3].fy;
        mPoint[5].fx = mWidth/2 + tempPoint[4].fx;
        mPoint[5].fy = mHeight - tempPoint[4].fy;
        mPoint[7].fx = mWidth/2 + tempPoint[5].fx;
        mPoint[7].fy = mHeight - tempPoint[5].fy;
    } else if (params.direction == 'r') {
        ALOGD("direction is right");
        mPoint[3].fx = mWidth/2 + tempPoint[0].fx;
        mPoint[3].fy = mHeight - tempPoint[0].fy;
        mPoint[5].fx = mWidth/2 + tempPoint[1].fx;
        mPoint[5].fy = mHeight - tempPoint[1].fy;
        mPoint[7].fx = mWidth/2 + tempPoint[2].fx;
        mPoint[7].fy = mHeight - tempPoint[2].fy;
        mPoint[2].fx = mWidth/2 + tempPoint[3].fx;
        mPoint[2].fy = mHeight - tempPoint[3].fy;
        mPoint[4].fx = mWidth/2 + tempPoint[4].fx;
        mPoint[4].fy = mHeight - tempPoint[4].fy;
        mPoint[6].fx = mWidth/2 + tempPoint[5].fx;
        mPoint[6].fy = mHeight - tempPoint[5].fy;
    }
    for(int i=0; i<8; i++) {
        ALOGD("screenPoint i=%d, x=%f, y=%f", i, mPoint[i].fx, mPoint[i].fy);
    }
#if 0
    mPaint->setColor(SK_ColorBLUE);
    mCanvas->drawPoint(getScreenXCoordinate(groundPoint[0].fx, groundPoint[0].fy)+mWidth/2,mHeight-getScreenYCoordinate(groundPoint[0].fy),*mPaint);
    mCanvas->drawPoint(getScreenXCoordinate(groundPoint[1].fx, groundPoint[1].fy)+mWidth/2,mHeight-getScreenYCoordinate(groundPoint[1].fy),*mPaint);
    mCanvas->drawPoint(getScreenXCoordinate(groundPoint[2].fx, groundPoint[2].fy)+mWidth/2,mHeight-getScreenYCoordinate(groundPoint[2].fy),*mPaint);

    mCanvas->drawPoint(mPoint[2].fx,mPoint[2].fy,*mPaint);
    mCanvas->drawPoint(mPoint[3].fx,mPoint[3].fy,*mPaint);
    mCanvas->drawPoint(mPoint[4].fx,mPoint[4].fy,*mPaint);
    mCanvas->drawPoint(mPoint[5].fx,mPoint[5].fy,*mPaint);
    mCanvas->drawPoint(mPoint[6].fx,mPoint[6].fy,*mPaint);
    mCanvas->drawPoint(mPoint[7].fx,mPoint[7].fy,*mPaint);
#endif
}

void RvcTrack::drawTrack(const DynamicTrack_t& params) {
    if (!mBitmap) {
        ALOGE("Error on RvcTrack::drawTrack()! Not initialized!!\n");
        return;
    }
    mBitmap->lockPixels();
#ifdef FOR_ANDROID_OO
	mCanvas->drawColor(SK_ColorTRANSPARENT, SkBlendMode::kClear);
#else
	mCanvas->drawColor(SK_ColorTRANSPARENT, SkXfermode::kClear_Mode);
#endif
    if (mDynamicTrack.angle == 0) {
        setStaicCoordinate();
    } else {
        setDynamicCoordinate(params);
    }

    //red
    mPaint->setColor(SK_ColorRED);
    mCanvas->drawLine(mPoint[0].fx, mPoint[0].fy, mPoint[2].fx, mPoint[2].fy, *mPaint);
    mCanvas->drawLine(mPoint[2].fx, mPoint[2].fy, mPoint[3].fx, mPoint[3].fy, *mPaint);
    mCanvas->drawLine(mPoint[3].fx, mPoint[3].fy, mPoint[1].fx, mPoint[1].fy, *mPaint);
    //yellow
    mPaint->setColor(SK_ColorYELLOW);
    mCanvas->drawLine(mPoint[2].fx, mPoint[2].fy, mPoint[4].fx, mPoint[4].fy, *mPaint);
    mCanvas->drawLine(mPoint[4].fx, mPoint[4].fy, mPoint[5].fx, mPoint[5].fy, *mPaint);
    mCanvas->drawLine(mPoint[5].fx, mPoint[5].fy, mPoint[3].fx, mPoint[3].fy, *mPaint);
    //green
    mPaint->setColor(SK_ColorGREEN);
    mCanvas->drawLine(mPoint[4].fx, mPoint[4].fy, mPoint[6].fx, mPoint[6].fy, *mPaint);
    mCanvas->drawLine(mPoint[6].fx, mPoint[6].fy, mPoint[7].fx, mPoint[7].fy, *mPaint);
    mCanvas->drawLine(mPoint[7].fx, mPoint[7].fy, mPoint[5].fx, mPoint[5].fy, *mPaint);

    mBitmap->notifyPixelsChanged();
    mBitmap->unlockPixels();
}

void RvcTrack::renderToGL() {
    if (!mBitmap) {
        ALOGE("Error on RvcTrack::render()! Not initialized!!\n");
        return;
    }
    int w = mBitmap->width();
    int h = mBitmap->height();
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mBitmap->getPixels());
    glDrawTexiOES(0, 0, 0, w, h);
}

void RvcTrack::setTrackParams(TrackParams_t* params) {
    mMutex->lock();
    memcpy(&mTrackParams, params, sizeof(TrackParams_t));
    mMutex->unlock();
    ALOGD("setTrackParams angle=%d, one=%d, two=%d, three=%d", mTrackParams.angle, mTrackParams.oneMeterPercent,
        mTrackParams.twoMeterPercent, mTrackParams.threeMeterPercent);
}

TrackParams_t RvcTrack::getTrackParams() {
    mMutex->lock();
    TrackParams_t params = mTrackParams;
    //ALOGD("getTrackParams angle=%d, one=%d, two=%d, three=%d", params.angle, params.oneMeterPercent,
    //    params.twoMeterPercent, params.threeMeterPercent);
    mMutex->unlock();
    return params;
}

void RvcTrack::setAngle(int value) {
    mMutex->lock();
    mTrackParams.angle = value;
    mMutex->unlock();
    ALOGD("setAngle angle=%d", mTrackParams.angle);
}

void RvcTrack::setOneMPercent(int value) {
    mMutex->lock();
    mTrackParams.oneMeterPercent = value;
    mMutex->unlock();
    ALOGD("setOneMPercent oneMeterPercent=%d", mTrackParams.oneMeterPercent);
}

void RvcTrack::setTwoMPercent(int value) {
    mMutex->lock();
    mTrackParams.twoMeterPercent = value;
    mMutex->unlock();
    ALOGD("setTwoMPercent twoMeterPercent=%d", mTrackParams.twoMeterPercent);
}

void RvcTrack::setThreeMPercent(int value) {
    mMutex->lock();
    mTrackParams.threeMeterPercent = value;
    mMutex->unlock();
    ALOGD("setThreeMPercent threeMeterPercent=%d", mTrackParams.threeMeterPercent);
}

void RvcTrack::adjustTrackParams(char adjustFlag, int value) {
    mMutex->lock();
    switch(adjustFlag) {
        case 'A':
            if (value == LEFT_KEY) {
                mTrackParams.angle++;
            } else if (value == RIGHT_KEY) {
                mTrackParams.angle--;
            }
            break;
        case 'B':
            if (value == UP_KEY) {
                mTrackParams.oneMeterPercent++;
            } else if (value == DOWN_KEY) {
                mTrackParams.oneMeterPercent--;
            }
            break;
        case 'C':
            if (value == UP_KEY) {
                mTrackParams.twoMeterPercent++;
            } else if (value == DOWN_KEY) {
                mTrackParams.twoMeterPercent--;
            }
            break;
        case 'D':
            if (value == UP_KEY) {
                mTrackParams.threeMeterPercent++;
            } else if (value == DOWN_KEY) {
                mTrackParams.threeMeterPercent--;
            }
            break;
        default:
            break;
    };
    mMutex->unlock();
    ALOGD("adjustTrackParams adjustFlag=%d, value=%d", adjustFlag, value);
}

void RvcTrack::setDynamicTrackParams(int direction, int angle) {
    mMutex->lock();
    mDynamicTrack.direction = direction;
    mDynamicTrack.angle = angle;
    mMutex->unlock();
    ALOGD("setDynamicTrackParams direction=%d, angle=%d", direction, angle);
}

DynamicTrack_t RvcTrack::getDynamicTrackParams() {
    mMutex->lock();
    DynamicTrack_t params = mDynamicTrack;
    mMutex->unlock();
    return params;
}

void RvcTrack::adjustDynamicTrackParams(char adjustFlag, int value) {
    mMutex->lock();
    if (value == LEFT_KEY) {
        if (mDynamicTrack.direction == 'l') {
            mDynamicTrack.angle++;
        } else if (mDynamicTrack.direction == 'r') {
            mDynamicTrack.angle--;
            if (mDynamicTrack.angle == 0) {
                mDynamicTrack.direction = '\0';
            }
        } else {
            mDynamicTrack.direction = 'l';
            mDynamicTrack.angle++;
        }
    } else if (value == RIGHT_KEY) {
        if (mDynamicTrack.direction == 'l') {
            mDynamicTrack.angle--;
            if (mDynamicTrack.angle == 0) {
                mDynamicTrack.direction = '\0';
            }
        } else if (mDynamicTrack.direction == 'r') {
            mDynamicTrack.angle++;
        } else {
            mDynamicTrack.direction = 'r';
            mDynamicTrack.angle++;
        }
    }
    mMutex->unlock();
    ALOGD("adjustTrackParams adjustFlag=%d, value=%d", adjustFlag, value);
}

void RvcTrack::showAllRvcTrackParams() {
    printf("Show All Rvc Track Params:\n");
    printf("Angle: %d\n", mTrackParams.angle);
    printf("OneMeterPercent: %d\n", mTrackParams.oneMeterPercent);
    printf("TwoMeterPercent: %d\n", mTrackParams.twoMeterPercent);
    printf("ThreeMeterPercent: %d\n", mTrackParams.threeMeterPercent);
}

void RvcTrack::draw() {
    // clear screen
    glShadeModel(GL_FLAT);
    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(mDisplay, mSurface);

    // Blend state
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBlendFunc(GL_ONE, GL_ZERO);
    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    init(mWidth, mHeight);

    nsecs_t frameDuration = s2ns(1) / mFps;
    DynamicTrack_t params;
    do {
        nsecs_t now = systemTime();
        params = getDynamicTrackParams();
        drawTrack(params);
        renderToGL();
        eglSwapBuffers(mDisplay, mSurface);
        const nsecs_t sleepTime = frameDuration - ns2us(systemTime() - now);
        if (sleepTime > 0)
            usleep(sleepTime/1000000);

        checkExit();
    } while (!exitPending());
}

void RvcTrack::checkExit() {
    char value[PROPERTY_VALUE_MAX];
    property_get(RVC_EXIT_PROP, value, "0");
    int exitnow = atoi(value);
    if (exitnow) {
        requestExit();
    }
}

bool RvcTrack::threadLoop() {
    draw();
    property_set(RVC_EXIT_PROP, "0");
    eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(mDisplay, mContext);
    eglDestroySurface(mDisplay, mSurface);
    mFlingerSurface.clear();
    mFlingerSurfaceControl.clear();
    eglTerminate(mDisplay);
    IPCThreadState::self()->stopProcess();
    return false;
}

// ---------------------------------------------------------------------------
}; //namespace android

