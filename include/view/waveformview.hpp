#ifndef DATAJOCKEY_WAVEFORM_VIEW_HPP
#define DATAJOCKEY_WAVEFORM_VIEW_HPP

#include <QGraphicsView>
#include <QString>

class QGraphicsScene;
class QResizeEvent;
class QGraphicsLineItem;

namespace DataJockey {
   namespace View {
      class WaveFormItem;

      class WaveFormView : public QGraphicsView {
         Q_OBJECT
         public:
            WaveFormView(QWidget * parent = NULL);
            virtual ~WaveFormView();
            void set_audio_file(const QString& file_name);
            void set_audio_frame(int frame);
            virtual void resizeEvent(QResizeEvent * event);
            virtual void wheelEvent(QWheelEvent * event);
            virtual void mouseMoveEvent(QMouseEvent * event);
            virtual void mousePressEvent(QMouseEvent * event);
            virtual void mouseReleaseEvent(QMouseEvent * event);
         signals:
            void seek_relative(int);
            void mouse_down(bool);
         private:
            int mLastMousePos;
            WaveFormItem * mWaveForm;
            QGraphicsScene * mScene;
            QGraphicsLineItem * mCursor;
      };
   }
}

#endif