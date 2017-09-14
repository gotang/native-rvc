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

// ---------------------------------------------------------------------------
RvcTrack::RvcTrack(TrackParams_t& params) : Thread(false) {
    mSession = new SurfaceComposerClient();
    mWidth = 0;
    mHeight = 0;
    mFps = 30;
    mTrackParams = params;
    mPaint = NULL;
    mCanvas = NULL;
    mBitmap = NULL;
    mTexture = 0;
    mMutex = new Mutex();
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
#if 0
    int trackH = h/2;
    mPoint[0] = {float(100), float(h)};
    mPoint[1] = {float(w-100), float(h)};
    mPoint[2] = {float(200), float(trackH/3*2+trackH)};
    mPoint[3] = {float(w-200), float(trackH/3*2+trackH)};
    mPoint[4] = {float(300), float(trackH/3+trackH)};
    mPoint[5] = {float(w-300), float(trackH/3+trackH)};
    mPoint[6] = {float(400), float(trackH)};
    mPoint[7] = {float(w-400), float(trackH)};
    for(int i=0; i<8; i++) {
        ALOGD("x=%f, y=%f", mPoint[i].fx, mPoint[i].fy);
    }
#endif
}

void RvcTrack::setPointsCoordinate(const TrackParams_t& params) {
    ALOGD("params angle=%d, one=%d, two=%d, three=%d", params.angle, params.oneMeterPercent,
        params.twoMeterPercent, params.threeMeterPercent);
    mPoint[0].fx = 100;
    mPoint[1].fx = mWidth - 100;
    mPoint[0].fy = mPoint[1].fy = mHeight;
    mPoint[2].fy = mPoint[3].fy = mHeight * (100 - params.oneMeterPercent)/100;
    mPoint[4].fy = mPoint[5].fy = mHeight * (100 - params.twoMeterPercent)/100;
    mPoint[6].fy = mPoint[7].fy = mHeight * (100 - params.threeMeterPercent)/100;
    mPoint[2].fx = tan((90 - params.angle) * M_PI / 180) * (mHeight - mPoint[2].fy) + mPoint[0].fx;
    mPoint[3].fx = mWidth - mPoint[2].fx;
    mPoint[4].fx = tan((90 - params.angle) * M_PI / 180) * (mHeight - mPoint[4].fy) + mPoint[0].fx;
    mPoint[5].fx = mWidth - mPoint[4].fx;
    mPoint[6].fx = tan((90 - params.angle) * M_PI / 180) * (mHeight - mPoint[6].fy) + mPoint[0].fx;
    mPoint[7].fx = mWidth - mPoint[6].fx;
    for(int i=0; i<8; i++) {
        ALOGD("x=%f, y=%f", mPoint[i].fx, mPoint[i].fy);
    }
}

void RvcTrack::drawTrack(const TrackParams_t& params) {
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
    setPointsCoordinate(params);
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
    //yellow
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
    ALOGD("getTrackParams angle=%d, one=%d, two=%d, three=%d", params.angle, params.oneMeterPercent,
        params.twoMeterPercent, params.threeMeterPercent);
    mMutex->unlock();
    return params;
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
    TrackParams_t params;
    do {
        nsecs_t now = systemTime();
        params = getTrackParams();
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

