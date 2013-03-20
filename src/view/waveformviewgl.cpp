#include "waveformviewgl.hpp"
#include "audiobuffer.hpp"
#include "defines.hpp"
#include <QtGui>
#include <QMutexLocker>
#include <stdlib.h>
#include <algorithm>

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE  0x809D
#endif

using namespace dj::view;

#include <iostream>
using std::cout;
using std::endl;

namespace {
  QColor color_interp(const QColor& start, const QColor& end, double dist) {
   if (dist <= 0.0)
    return start;
   else if (dist >= 1.0)
    return end;
   QColor r;
   double h,s,v;
   double s_h, s_s, s_v;
   double e_h, e_s, e_v;
   start.getHsvF(&s_h, &s_s, &s_v);
   end.getHsvF(&e_h, &e_s, &e_v);
   s_h = dj::clamp(s_h, 0.0, 1.0);
   s_s = dj::clamp(s_s, 0.0, 1.0);
   s_v = dj::clamp(s_v, 0.0, 1.0);

   e_h = dj::clamp(e_h, 0.0, 1.0);
   e_s = dj::clamp(e_s, 0.0, 1.0);
   e_v = dj::clamp(e_v, 0.0, 1.0);

   h = dj::linear_interp(s_h, e_h, dist);
   s = dj::linear_interp(s_s, e_s, dist);
   v = dj::linear_interp(s_v, e_v, dist);

   r.setHsvF(h, s, v);
   return r;
  }
}

WaveFormViewGL::WaveFormViewGL(QWidget * parent, bool vertical, bool full) :
  QGLWidget(parent),
  mMutex(),
  mHeight(100),
  mWidth(400),
  mCursorOffset(50),
  mVertical(vertical),
  mFullView(full),
  mFirstLineIndex(0),
  mVerticiesValid(false),
  mBeatBuffer(),
  mBeatVerticies(),
  mBeatVerticiesValid(false),
  mSampleRate(44100.0),
  mFramesPerLine(256),
  mFrame(0),
  backgroudColor(Qt::black),
  waveformColor(Qt::darkRed),
  cursorColor(Qt::blue),
  centerLineColor(Qt::green),
  beatsColor(Qt::yellow),
  mLastMousePos(0)
{
  if (mFullView)
   mCursorOffset = 0;
}

QSize WaveFormViewGL::minimumSizeHint() const { return QSize(100, 100); }
QSize WaveFormViewGL::sizeHint() const { return mVertical ? QSize(100, 400) : QSize(400, 100); }
void WaveFormViewGL::setVertical(bool vert) { mVertical = vert; }

void WaveFormViewGL::clear_audio() {
  QMutexLocker lock(&mMutex);
  mAudioBuffer.reset();
  mVerticiesValid = false;
  update();
}

void WaveFormViewGL::set_buffers(audio::AudioBufferPtr audio_buffer, audio::BeatBufferPtr beat_buffer) {
  QMutexLocker lock(&mMutex);
  mAudioBuffer.swap(audio_buffer);
  mBeatBuffer.swap(beat_buffer);

  if (mFullView) {
    if (mAudioBuffer) {
     mFramesPerLine = mAudioBuffer->length() / (mVertical ? mHeight : mWidth);
     float sample_rate = mAudioBuffer->sample_rate();
     if (sample_rate != mSampleRate)
      mSampleRate = sample_rate;
    }
    update_colors();
  } else {
    if (mBeatBuffer && mBeatBuffer->length() > 2) {
     //XXX make configurable
     double frames_per_view = mBeatBuffer->median_difference() * 8.0 * mSampleRate;
     mFramesPerLine = frames_per_view / (mVertical ? mHeight : mWidth);
     mVerticiesValid = false;
     update_beats();
    } else
     mBeatVerticiesValid = false;

    if (mAudioBuffer) {
     float sample_rate = mAudioBuffer->sample_rate();
     if (sample_rate != mSampleRate) {
      mSampleRate = sample_rate;
      //if the beat buffer was set before the audio buffer, we'll need to redraw
      if (mBeatVerticiesValid)
        update_beats();
     }
    }
  }
  mVerticiesValid = false;
  clear_markers();
  clear_loops();

  update();
}

void WaveFormViewGL::clear_beats() {
  QMutexLocker lock(&mMutex);
  mBeatVerticiesValid = false;
  update();
}
        
void WaveFormViewGL::set_frame(int frame) {
  int prev = mFrame;
  mFrame = frame;
  if (mFullView) {
    int offset = frame / mFramesPerLine;
    if (offset != mCursorOffset) {
     mCursorOffset = offset;
     update();
    }
  } else {
    if (prev > mFrame || mFrame >= prev + mFramesPerLine)
     update();
  }
}

void WaveFormViewGL::set_frames_per_line(int num_frames) {
  if (num_frames < 1)
    num_frames = 1;
  if (mFramesPerLine != num_frames) {
    mVerticiesValid = false;
    mFramesPerLine = num_frames;
    update();
  }
}

void WaveFormViewGL::clear_markers() {
  mMarkers.clear();
  update_markers();
  update();
}

void WaveFormViewGL::add_marker(int id, int frame, QColor color) {
  bool found = false;
  for (int i = 0; i < mMarkers.size(); i++) {
    if (mMarkers[i].id == id) {
      mMarkers[i].frame = frame;
      mMarkers[i].color = color;
      found = true;
      break;
    }
  }
  if (!found)
    mMarkers << marker_t(id, frame, color);
  update_markers();
  update();
}

void WaveFormViewGL::remove_marker(int id) {
  QList<marker_t>::iterator it = mMarkers.begin();
  while (it != mMarkers.end()) {
    if (it->id == id)
      it = mMarkers.erase(it);
    else
      it++;
  }
  update_markers();
  update();
}

void WaveFormViewGL::clear_loops() {
  mLoopVerticies.clear();
  mLoops.clear();
  update();
}

void WaveFormViewGL::add_loop(int id, int frame_start, int frame_end) {
  loop_t loop(frame_start, frame_end, Qt::cyan); //XXX make color configurable
  mLoops[id] = loop;

  GLfloat x0 = static_cast<GLfloat>(frame_start) / static_cast<GLfloat>(mFramesPerLine);
  GLfloat x1 = static_cast<GLfloat>(frame_end) / static_cast<GLfloat>(mFramesPerLine);
  gl_rect_t rect(x0, static_cast<GLfloat>(1.0), x1, static_cast<GLfloat>(-1.0));

  mLoopVerticies[id] = rect;
}

void WaveFormViewGL::remove_loop(int id) {
  {
    QHash<int, loop_t>::iterator it = mLoops.find(id);
    if (it != mLoops.end())
      mLoops.erase(it);
  }

  mLoopVerticies.erase(id);
  update();
}

void WaveFormViewGL::initializeGL(){
  QMutexLocker lock(&mMutex);

  qglClearColor(backgroudColor);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, mWidth, mHeight, 0, 0, 1);
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_MULTISAMPLE);
  glDisable(GL_DEPTH_TEST);

  mVerticies.resize(4 * (mVertical ? mHeight : mWidth));
  if (mFullView) {
    mVertexColors.resize((2 * 3) * (mVertical ? mHeight : mWidth));
    update_colors();
  }
}

void WaveFormViewGL::paintGL(){
  QMutexLocker lock(&mMutex);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glLoadIdentity();

  if (mVertical) {
    if (mFullView) {
      glTranslatef(mWidth / 2, 0, 0.0);
      glRotatef(90.0, 0.0, 0.0, 1.0);
    } else {
      glTranslatef(mWidth / 2, mHeight, 0.0);
      glRotatef(-90.0, 0.0, 0.0, 1.0);
    }
    //-1..1 in the y direction
    glScalef(1.0, mWidth / 2, 1.0);
  } else {
    glTranslatef(0.0, mHeight / 2, 0.0);
    //-1..1 in the x direction
    glScalef(1.0, mHeight / 2, 1.0);
  }

  if (mAudioBuffer) {
    //TODO treat vertices as a circular buffer and only update what we need to
    update_waveform();

    if (mVerticiesValid) {
      //draw waveform
      glPushMatrix();
      glTranslatef(-mVerticies[mFirstLineIndex * 4], 0, 0);
      qglColor(waveformColor);
      glLineWidth(1.0);

      if (mFullView) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(3, GL_FLOAT, 0, &mVertexColors.front());
        glVertexPointer(2, GL_FLOAT, 0, &mVerticies.front());
        glDrawArrays(GL_LINES, 0, mVerticies.size() / 2);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
      } else {
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, &mVerticies.front());
        glDrawArrays(GL_LINES, 0, mVerticies.size() / 2);
        glDisableClientState(GL_VERTEX_ARRAY);
      }

      //draw beats if we have them
      if (mBeatVerticiesValid) {
        glPushMatrix();
        qglColor(beatsColor);
        glLineWidth(1.0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, &mBeatVerticies.front());
        glDrawArrays(GL_LINES, 0, mBeatVerticies.size() / 2);
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopMatrix();
      }

      //draw markers
      if (!mMarkers.empty()) {
        glPushMatrix();
        //glTranslatef(-mVerticies[mFirstLineIndex * 4], 0, 0);
        glLineWidth(2.0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(3, GL_FLOAT, 0, &mMarkerColors.front());
        glVertexPointer(2, GL_FLOAT, 0, &mMarkerVerticies.front());
        glDrawArrays(GL_LINES, 0, mMarkerVerticies.size() / 2);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopMatrix();
      }

      if (!mLoopVerticies.empty()) {
        glPushMatrix();
        //XXX make color configurable
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (mFullView)
          qglColor(QColor(0, 255, 255, 255));
        else
          qglColor(QColor(0, 255, 255, 64));

        glLineWidth(1.0);

        glEnableClientState(GL_VERTEX_ARRAY);
        //glEnableClientState(GL_COLOR_ARRAY);
        //glColorPointer(3, GL_FLOAT, 0, &mMarkerColors.front());

        for (std::map<int, gl_rect_t>::iterator it = mLoopVerticies.begin(); it != mLoopVerticies.end(); it++) {
          glVertexPointer(2, GL_FLOAT, 0, it->second.points);
          glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        //glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);

        glPopMatrix();
      }


      glPopMatrix();
    }
  }

  //draw cursor
  qglColor(cursorColor);
  glLineWidth(2.0);
  glBegin(GL_LINES);
  glVertex2f(mCursorOffset, -1.0);
  glVertex2f(mCursorOffset, 1.0);
  glEnd();

  //draw center line
  qglColor(centerLineColor);
  glLineWidth(1.0);
  glBegin(GL_LINES);
  glVertex2f(0, 0);
  glVertex2f(mVertical ? mHeight : mWidth, 0);
  glEnd();

  glFlush();
}

void WaveFormViewGL::resizeGL(int width, int height) {
  QMutexLocker lock(&mMutex);
  bool changed = false;

  if (mVertical) {
    if (mHeight != height) {
      changed = true;
      mVerticies.resize(4 * height);
      if (mFullView) {
        mVertexColors.resize(height * (3 * 2));
        update_colors();
      }
    }
  } else {
    if (mWidth != width) {
      changed = true;
      mVerticies.resize(4 * width);
      if (mFullView) {
        mVertexColors.resize(width * (3 * 2));
        update_colors();
      }
    }
  }

  if (changed) {
    update_markers();
    update_loops();
    mVerticiesValid = false;
  }

  mWidth = width;
  mHeight = height;

  if (mFullView && mAudioBuffer)
    mFramesPerLine = mAudioBuffer->length() / (mVertical ? mHeight : mWidth);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, mWidth, mHeight, 0, 0, 1);
  glMatrixMode(GL_MODELVIEW);
  glDisable(GL_DEPTH_TEST);

  glViewport(0, 0, mWidth, mHeight);
}


void WaveFormViewGL::mouseMoveEvent(QMouseEvent * event) {
  int diff = event->y() - mLastMousePos;
  mLastMousePos = event->y();
  int frames = mFramesPerLine * diff;
  emit(seek_relative(frames));
}

void WaveFormViewGL::mousePressEvent(QMouseEvent * event) {
  mLastMousePos = event->y();
  emit(mouse_down(true));
  emit(frame_clicked(mFramesPerLine * mLastMousePos));
}

void WaveFormViewGL::mouseReleaseEvent(QMouseEvent * /* event */) {
  emit(mouse_down(false));
}

void WaveFormViewGL::update_waveform() {
  int first_line = ((mFrame / mFramesPerLine) - mCursorOffset);
  const int total_lines = (int)mVerticies.size() / 4;
  int compute_lines = total_lines;
  int compute_line_offset = 0;

  if (mVerticiesValid) {
    //XXX should we just store this value to not have to convert back from a float?
    int current_first_line = (int)mVerticies[mFirstLineIndex * 4];
    if (current_first_line <= first_line) {
      //our new first line is at or after our current first line
      //there is data to keep
      if (current_first_line + total_lines > first_line) {
        int offset = first_line - current_first_line;
        mFirstLineIndex = (mFirstLineIndex + offset) % total_lines;
        //the new data to fill is between first_line and current_first_line
        compute_lines = offset;
        compute_line_offset = total_lines - offset + mFirstLineIndex;
        first_line = current_first_line + total_lines;
      } else {
        //totally wipe out what we have
        mFirstLineIndex = 0;
      }
    } else {
      //our new first line is before our current first line
      //there is data to keep
      if (first_line + total_lines > current_first_line) {
        int offset = current_first_line - first_line;
        mFirstLineIndex -= offset;
        if (mFirstLineIndex < 0)
          mFirstLineIndex += total_lines;
        //the new data to fill is between first_line and current_first_line
        compute_lines = offset;
        compute_line_offset = mFirstLineIndex;
      } else {
        //totally wipe out what we have
        mFirstLineIndex = 0;
      }
    }
  } else {
    if (first_line >= 0) {
      mFirstLineIndex = first_line % total_lines;
    } else {
      mFirstLineIndex = first_line;
      while(mFirstLineIndex < 0)
        mFirstLineIndex += total_lines;
    }
    compute_line_offset = mFirstLineIndex;
  }

  //this is only called with a valid audio buffer
  for(int line = 0; line < compute_lines; line++) {
    int index = ((line + compute_line_offset) % total_lines) * 4;
    int line_index = line + first_line;
    GLfloat value = line_value(line_index);
    mVerticies[index] = mVerticies[index + 2] = line_index;
    mVerticies[index + 1] = value;
    mVerticies[index + 3] = -value;
  }
  mVerticiesValid = true;
}

void WaveFormViewGL::update_beats() {
  mBeatVerticies.resize(4 * mBeatBuffer->length());
  for(unsigned int i = 0; i < mBeatBuffer->length(); i++) {
    int line_index = i * 4;
    GLfloat pos = (mSampleRate * mBeatBuffer->at(i)) / mFramesPerLine;
    mBeatVerticies[line_index] = mBeatVerticies[line_index + 2] = pos;
    mBeatVerticies[line_index + 1] = static_cast<GLfloat>(1.0);
    mBeatVerticies[line_index + 3] = static_cast<GLfloat>(-1.0);
  }
  mBeatVerticiesValid = true;
}

GLfloat WaveFormViewGL::line_value(int line_index) {
  //this is only called with a valid audio buffer
  int start_frame = line_index * mFramesPerLine;

  if (start_frame < 0 || start_frame >= (int)mAudioBuffer->length()) {
    return (GLfloat)0.0;
  } else {
    int end_frame = std::min(start_frame + mFramesPerLine, (int)mAudioBuffer->length());
    float value = 0;
    for (int frame = start_frame; frame < end_frame; frame++) {
      value = std::max(value, mAudioBuffer->sample(0, frame));
      value = std::max(value, mAudioBuffer->sample(1, frame));
    }
    return (GLfloat)value;
  }
}

void WaveFormViewGL::update_colors() {
  int colored_lines = mVertexColors.size() / 6;
  const QColor waveform_color = waveformColor;
  float normal_color[3];
  normal_color[0] = waveform_color.redF();
  normal_color[1] = waveform_color.greenF();
  normal_color[2] = waveform_color.blueF();

  const QColor slow_color = Qt::blue;
  const QColor fast_color = Qt::magenta;
  const QColor tempo_color = Qt::green;

  for (int line = 0; line < colored_lines; line++) {
   int i = line * 6;
   memcpy(&mVertexColors[i], normal_color, 3 * sizeof(float));
   memcpy(&mVertexColors[i + 3], normal_color, 3 * sizeof(float));
  }

  if (mBeatBuffer) {
   const double median = mBeatBuffer->median_difference();
   //first compute the variation in tempo from the neighboring beat
   //then the variation in tempo from the median beat
   //XXX make this configurable
   for (unsigned int i = 2; i < mBeatBuffer->length(); i++) {
    double diff0 = mBeatBuffer->at(i - 1) - mBeatBuffer->at(i - 2);
    double diff1 = mBeatBuffer->at(i) - mBeatBuffer->at(i - 1);
    double off_val = fabsl(diff1 - diff0);
    QColor qcolor = color_interp(waveform_color, tempo_color, clamp(off_val * 1000, 0.0, 1.0)); 
    float color[3];
    color[0] = qcolor.redF();
    color[1] = qcolor.greenF();
    color[2] = qcolor.blueF();

    int start_line = (mSampleRate * mBeatBuffer->at(i - 2)) / mFramesPerLine;
    int end_line = (mSampleRate * mBeatBuffer->at(i)) / mFramesPerLine;
    for (int line = start_line; line <= std::min(end_line, colored_lines); line++) {
      int line_index = line * 6;
      memcpy(&mVertexColors[line_index], color, sizeof(float) * 3);
      //mVertexColors[line_index + 1] = mVertexColors[line_index + 1 + 3] = clamp(off_val * 1000.0, 0.0, 1.0);
    }


    double mdiff = mBeatBuffer->at(i - 1) - mBeatBuffer->at(i - 2);
    off_val = clamp((median - mdiff) * 100.0, -1.0, 1.0);
    if (off_val > 0)
      qcolor = color_interp(waveform_color, fast_color, off_val);
    else
      qcolor = color_interp(waveform_color, slow_color, -off_val);
    color[0] = qcolor.redF();
    color[1] = qcolor.greenF();
    color[2] = qcolor.blueF();

    start_line = (mSampleRate * mBeatBuffer->at(i - 2)) / mFramesPerLine;
    end_line = (mSampleRate * mBeatBuffer->at(i - 1)) / mFramesPerLine;
    for (int line = start_line; line <= std::min(end_line, colored_lines); line++) {
      int line_index = line * 6;
      memcpy(&mVertexColors[line_index + 3], color, sizeof(float) * 3);
    }
   }
  }
}

void WaveFormViewGL::update_markers() {
  //XXX is there a better way?
  mMarkerVerticies.resize(4 * mMarkers.size());
  mMarkerColors.resize((2 * 3) * mMarkers.size());

  for (int i = 0; i < mMarkers.size(); i++) {
    int line_index = i * 4;
    GLfloat pos = static_cast<GLfloat>(mMarkers[i].frame) / static_cast<GLfloat>(mFramesPerLine);
    mMarkerVerticies[line_index] = mMarkerVerticies[line_index + 2] = pos;
    mMarkerVerticies[line_index + 1] = static_cast<GLfloat>(1.0);
    mMarkerVerticies[line_index + 3] = static_cast<GLfloat>(-1.0);

    //set up color
    int color_index = i * 6;
    float color[3];
    color[0] = mMarkers[i].color.redF();
    color[1] = mMarkers[i].color.greenF();
    color[2] = mMarkers[i].color.blueF();
    memcpy(&mMarkerColors[color_index], color, 3 * sizeof(float));
    memcpy(&mMarkerColors[color_index + 3], color, 3 * sizeof(float));
  }
}

void WaveFormViewGL::update_loops() {
  for(QHash<int, loop_t>::iterator it = mLoops.begin(); it != mLoops.end(); it++) {
    GLfloat x0 = static_cast<GLfloat>(it.value().frame_start) / static_cast<GLfloat>(mFramesPerLine);
    GLfloat x1 = static_cast<GLfloat>(it.value().frame_end) / static_cast<GLfloat>(mFramesPerLine);
    gl_rect_t rect(x0, static_cast<GLfloat>(1.0), x1, static_cast<GLfloat>(-1.0));

    mLoopVerticies[it.key()] = rect;
  }
}
