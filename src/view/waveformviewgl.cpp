#include "waveformviewgl.hpp"
#include "audiobuffer.hpp"
#include <QtGui>
#include <QMutexLocker>
#include <stdlib.h>
#include <algorithm>

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE  0x809D
#endif

using namespace DataJockey::View;

WaveFormViewGL::WaveFormViewGL(QWidget * parent, bool vertical) :
   QGLWidget(parent),
   mHeight(100),
   mWidth(400),
   mCursorOffset(50),
   mVertical(vertical),
   mFramesPerLine(256),
   mFrame(0),
   mColorBackgroud(QColor::fromRgb(0,0,0)),
   mColorWaveform(QColor::fromRgb(255,0,0).dark()),
   mColorCursor(QColor::fromRgb(0,255,0)),
   mColorCenterLine(QColor::fromRgb(0,0,255)),
   mAudioBufferMutex()
{
}

QSize WaveFormViewGL::minimumSizeHint() const { return QSize(100, 100); }
QSize WaveFormViewGL::sizeHint() const { return mVertical ? QSize(100, 400) : QSize(400, 100); }
void WaveFormViewGL::setVertical(bool vert) { mVertical = vert; }

void WaveFormViewGL::clear_audio() {
   QMutexLocker lock(&mAudioBufferMutex);
   mAudioBuffer.release();
   update();
}

void WaveFormViewGL::set_audio_file(QString file_name) { 
   QMutexLocker lock(&mAudioBufferMutex);
   mAudioBuffer.reset(file_name); 
   update();
}

void WaveFormViewGL::set_frame(int frame) {
   int prev = mFrame;
   mFrame = frame;
   if (prev / mFramesPerLine != mFrame / mFramesPerLine) {
      update();
   }
}

void WaveFormViewGL::initializeGL(){
   QMutexLocker lock(&mAudioBufferMutex);

   qglClearColor(mColorBackgroud);

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0, mWidth, mHeight, 0, 0, 1);
   glMatrixMode(GL_MODELVIEW);
   glEnable(GL_MULTISAMPLE);
   glDisable(GL_DEPTH_TEST);

   mVerticies.resize(4 * (mVertical ? mHeight : mWidth));
}

void WaveFormViewGL::paintGL(){
   QMutexLocker lock(&mAudioBufferMutex);

   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glLoadIdentity();

   if (mVertical) {
      glTranslatef(mWidth / 2, mHeight, 0.0);
      glRotatef(-90.0, 0.0, 0.0, 1.0);
      //-1..1 in the y direction
      glScalef(1.0, mWidth / 2, 1.0);
   } else {
      glTranslatef(0.0, mHeight / 2, 0.0);
      //-1..1 in the x direction
      glScalef(1.0, mHeight / 2, 1.0);
   }

   if (mAudioBuffer.valid()) {
      //TODO treat vertices as a circular buffer and only update what we need to
      update_waveform();

      //draw waveform
      qglColor(mColorWaveform);
      glLineWidth(1.0);
      glEnableClientState(GL_VERTEX_ARRAY);
      glVertexPointer(2, GL_FLOAT, 0, &mVerticies.front());
      glDrawArrays(GL_LINES, 0, mVerticies.size() / 2);
   }

   //draw cursor
   qglColor(mColorCursor);
   glLineWidth(2.0);
   glBegin(GL_LINES);
   glVertex2f(mCursorOffset, -1.0);
   glVertex2f(mCursorOffset, 1.0);
   glEnd();

   //draw center line
   qglColor(mColorCenterLine);
   glLineWidth(1.0);
   glBegin(GL_LINES);
   glVertex2f(0, 0);
   glVertex2f(mVertical ? mHeight : mWidth, 0);
   glEnd();

   glFlush();
}

void WaveFormViewGL::resizeGL(int width, int height) {
   QMutexLocker lock(&mAudioBufferMutex);

   mWidth = width;
   mHeight = height;

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0, mWidth, mHeight, 0, 0, 1);
   glMatrixMode(GL_MODELVIEW);
   glDisable(GL_DEPTH_TEST);

   glViewport(0, 0, mWidth, mHeight);

   mVerticies.resize(4 * (mVertical ? mHeight : mWidth));
}

/*
void WaveFormViewGL::mousePressEvent(QMouseEvent *event){
}

void WaveFormViewGL::mouseMoveEvent(QMouseEvent *event){
}
*/

void WaveFormViewGL::update_waveform() {
   int first_frame = mFrame - mCursorOffset * mFramesPerLine;

   //this is only called with a valid audio buffer
   for(unsigned int line = 0; line < mVerticies.size() / 4; line++) {
      unsigned int index = line * 4;
      int start_frame = first_frame + line * mFramesPerLine;

      if (start_frame < 0 || start_frame >= (int)mAudioBuffer->length()) {
         mVerticies[index] = 
            mVerticies[index + 1] =
            mVerticies[index + 2] = 
            mVerticies[index + 3] = -1;
      } else {
         int end_frame = std::min(start_frame + mFramesPerLine, mAudioBuffer->length());
         float value = 0;
         for (int frame = start_frame; frame < end_frame; frame++) {
            value = std::max(value, mAudioBuffer->sample(0, frame));
            value = std::max(value, mAudioBuffer->sample(1, frame));
         }
         mVerticies[index] = mVerticies[index + 2] = line;
         mVerticies[index + 1] = value;
         mVerticies[index + 3] = -value;
      }
   }
}
